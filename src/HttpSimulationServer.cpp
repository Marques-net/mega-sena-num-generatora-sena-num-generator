#include "HttpSimulationServer.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LotteryMachine.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct TrajectoryPoint {
    double time{0.0};
    int id{0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
    std::string status;
};

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string urlDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long decodedByte = std::strtol(hex.c_str(), &end, 16);
            if (end != hex.c_str() && *end == '\0') {
                decoded.push_back(static_cast<char>(decodedByte));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return decoded;
}

std::unordered_map<std::string, std::string> parseQuery(const std::string& query) {
    std::unordered_map<std::string, std::string> values;
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t end = query.find('&', start);
        const std::string pair = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = urlDecode(pair.substr(0, equals));
            const std::string value = equals == std::string::npos ? "" : urlDecode(pair.substr(equals + 1));
            values[lowerCopy(key)] = value;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return values;
}

bool parseDoubleValue(const std::string& raw, double& value) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(raw.c_str(), &end);
    if (errno != 0 || end == raw.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseUInt64Value(const std::string& raw, std::uint64_t& value) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(raw.c_str(), &end, 10);
    if (errno != 0 || end == raw.c_str() || *end != '\0') {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

std::string escapeRegexName(const std::string& name) {
    std::string escaped;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

bool extractJsonDouble(const std::string& body, const std::string& key, double& value) {
    const std::regex pattern("\"" + escapeRegexName(key) + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(body, match, pattern)) {
        return false;
    }
    return parseDoubleValue(match[1].str(), value);
}

bool extractJsonUInt64(const std::string& body, const std::string& key, std::uint64_t& value) {
    const std::regex pattern("\"" + escapeRegexName(key) + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(body, match, pattern)) {
        return false;
    }
    return parseUInt64Value(match[1].str(), value);
}

bool extractJsonString(const std::string& body, const std::string& key, std::string& value) {
    const std::regex pattern("\"" + escapeRegexName(key) + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(body, match, pattern)) {
        return false;
    }
    value = match[1].str();
    return true;
}

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

std::uint64_t defaultSeed() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now);
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << c;
            break;
        }
    }
    return out.str();
}

std::string vectorToJson(const std::vector<int>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
    return out.str();
}

std::string formatTimestampForPath() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis);
}

SimulationConfig defaultServiceConfig(const SimulationConfig& baseConfig) {
    SimulationConfig config = baseConfig;
    if (config.timeStep == 0.0005) {
        config.timeStep = 0.001;
    }
    if (config.maxDuration == 120.0) {
        config.maxDuration = 30.0;
    }
    if (config.minMixTime == 10.0) {
        config.minMixTime = 3.0;
    }
    if (config.csvInterval == 0.02) {
        config.csvInterval = 0.08;
    }
    if (config.upwardJetForce == 1.05) {
        config.upwardJetForce = 1.18;
    }
    if (config.turbulenceForce == 0.28) {
        config.turbulenceForce = 0.34;
    }
    if (config.outputDir == "output") {
        config.outputDir = "/tmp/mega-sena-runs";
    }
    config.seed = defaultSeed();
    return config;
}

SimulationConfig configFromRequest(const SimulationConfig& baseConfig, const HttpRequest& request) {
    SimulationConfig config = defaultServiceConfig(baseConfig);
    const auto query = parseQuery(request.query);

    auto setDouble = [&](const std::string& queryKey, const std::string& jsonKey, double& target) {
        double value = 0.0;
        const auto it = query.find(lowerCopy(queryKey));
        if (it != query.end() && parseDoubleValue(it->second, value)) {
            target = value;
            return;
        }
        if (extractJsonDouble(request.body, jsonKey, value)) {
            target = value;
        }
    };

    auto setUInt64 = [&](const std::string& queryKey, const std::string& jsonKey, std::uint64_t& target) {
        std::uint64_t value = 0;
        const auto it = query.find(lowerCopy(queryKey));
        if (it != query.end() && parseUInt64Value(it->second, value)) {
            target = value;
            return;
        }
        if (extractJsonUInt64(request.body, jsonKey, value)) {
            target = value;
        }
    };

    setUInt64("seed", "seed", config.seed);
    setDouble("dt", "timeStep", config.timeStep);
    setDouble("timeStep", "timeStep", config.timeStep);
    setDouble("maxTime", "maxTime", config.maxDuration);
    setDouble("maxDuration", "maxDuration", config.maxDuration);
    setDouble("minMixTime", "minMixTime", config.minMixTime);
    setDouble("csvInterval", "csvInterval", config.csvInterval);
    setDouble("jet", "jet", config.upwardJetForce);
    setDouble("upwardJetForce", "upwardJetForce", config.upwardJetForce);
    setDouble("turbulence", "turbulence", config.turbulenceForce);
    setDouble("turbulenceForce", "turbulenceForce", config.turbulenceForce);

    std::string captureMode;
    const auto captureModeIt = query.find("capturemode");
    if (captureModeIt != query.end()) {
        captureMode = captureModeIt->second;
    } else {
        extractJsonString(request.body, "captureMode", captureMode);
    }
    if (captureMode == "outlet" || captureMode == "top-side") {
        config.captureMode = captureMode;
    }

    config.timeStep = clampValue(config.timeStep, 0.0005, 0.005);
    config.maxDuration = clampValue(config.maxDuration, 5.0, 120.0);
    config.minMixTime = clampValue(config.minMixTime, 1.0, std::max(1.0, config.maxDuration - 0.5));
    config.csvInterval = clampValue(config.csvInterval, 0.02, 0.50);
    config.upwardJetForce = clampValue(config.upwardJetForce, 0.20, 3.0);
    config.turbulenceForce = clampValue(config.turbulenceForce, 0.0, 1.2);

    const std::filesystem::path baseOutputDir(config.outputDir);
    config.outputDir = (baseOutputDir / ("run-" + formatTimestampForPath() + "-" + std::to_string(config.seed))).string();
    return config;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

std::vector<TrajectoryPoint> readTrajectorySample(const SimulationConfig& config) {
    std::ifstream in(std::filesystem::path(config.outputDir) / "trajectory.csv");
    if (!in) {
        return {};
    }

    std::string line;
    std::getline(in, line);

    const int expectedSnapshots = std::max(1, static_cast<int>(std::ceil(config.maxDuration / config.csvInterval)));
    const int snapshotModulo = std::max(1, expectedSnapshots / 90);
    const int ballCount = std::max(1, config.ballCount);

    std::vector<TrajectoryPoint> points;
    points.reserve(5400);

    std::size_t row = 0;
    while (std::getline(in, line)) {
        const int snapshotIndex = static_cast<int>(row / static_cast<std::size_t>(ballCount));
        ++row;
        if (snapshotIndex % snapshotModulo != 0) {
            continue;
        }

        const std::vector<std::string> fields = splitCsvLine(line);
        if (fields.size() < 9) {
            continue;
        }

        TrajectoryPoint point;
        try {
            point.time = std::stod(fields[0]);
            point.id = std::stoi(fields[1]);
            point.x = std::stod(fields[2]);
            point.y = std::stod(fields[3]);
            point.z = std::stod(fields[4]);
            point.status = fields[8];
            points.push_back(std::move(point));
        } catch (const std::exception&) {
            continue;
        }
    }
    return points;
}

std::string trajectoryToJson(const std::vector<TrajectoryPoint>& points) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(5);
    out << '[';
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "{\"t\":" << points[i].time
            << ",\"id\":" << points[i].id
            << ",\"x\":" << points[i].x
            << ",\"y\":" << points[i].y
            << ",\"z\":" << points[i].z
            << ",\"status\":\"" << jsonEscape(points[i].status) << "\"}";
    }
    out << ']';
    return out.str();
}

std::string simulationResponseJson(const SimulationConfig& config, const LotteryMachine& machine, long long elapsedMs) {
    std::vector<int> sorted = machine.extractedBalls();
    std::sort(sorted.begin(), sorted.end());
    const std::vector<TrajectoryPoint> trajectory = readTrajectorySample(config);
    const bool complete = static_cast<int>(machine.extractedBalls().size()) == config.ballsToExtract;

    std::ostringstream out;
    out << std::fixed << std::setprecision(5);
    out << "{"
        << "\"status\":\"" << (complete ? "complete" : "partial") << "\","
        << "\"complete\":" << (complete ? "true" : "false") << ','
        << "\"seed\":" << config.seed << ','
        << "\"finalTime\":" << machine.finalTime() << ','
        << "\"elapsedMs\":" << elapsedMs << ','
        << "\"captureMode\":\"" << jsonEscape(config.captureMode) << "\","
        << "\"numbers\":" << vectorToJson(machine.extractedBalls()) << ','
        << "\"sortedNumbers\":" << vectorToJson(sorted) << ','
        << "\"config\":{"
        << "\"timeStep\":" << config.timeStep << ','
        << "\"maxTime\":" << config.maxDuration << ','
        << "\"minMixTime\":" << config.minMixTime << ','
        << "\"csvInterval\":" << config.csvInterval << ','
        << "\"jet\":" << config.upwardJetForce << ','
        << "\"turbulence\":" << config.turbulenceForce
        << "},"
        << "\"trajectory\":" << trajectoryToJson(trajectory)
        << "}";
    return out.str();
}

std::size_t contentLengthFromHeader(const std::string& header) {
    std::istringstream stream(header);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = lowerCopy(trim(line.substr(0, colon)));
        if (key != "content-length") {
            continue;
        }
        const std::string raw = trim(line.substr(colon + 1));
        char* end = nullptr;
        const unsigned long value = std::strtoul(raw.c_str(), &end, 10);
        if (end != raw.c_str() && *end == '\0') {
            return static_cast<std::size_t>(value);
        }
    }
    return 0;
}

#ifndef _WIN32
bool recvMore(int clientFd, std::string& buffer) {
    char chunk[8192];
    const ssize_t received = ::recv(clientFd, chunk, sizeof(chunk), 0);
    if (received <= 0) {
        return false;
    }
    buffer.append(chunk, static_cast<std::size_t>(received));
    return true;
}

std::string readRawRequest(int clientFd) {
    std::string buffer;
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (!recvMore(clientFd, buffer)) {
            return buffer;
        }
        if (buffer.size() > 1024 * 1024) {
            throw std::runtime_error("request header too large");
        }
    }

    const std::string header = buffer.substr(0, headerEnd + 4);
    const std::size_t contentLength = contentLengthFromHeader(header);
    const std::size_t expectedSize = headerEnd + 4 + contentLength;
    while (buffer.size() < expectedSize) {
        if (!recvMore(clientFd, buffer)) {
            break;
        }
        if (buffer.size() > 4 * 1024 * 1024) {
            throw std::runtime_error("request body too large");
        }
    }
    if (buffer.size() > expectedSize) {
        buffer.resize(expectedSize);
    }
    return buffer;
}

HttpRequest parseRequest(const std::string& raw) {
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        throw std::runtime_error("invalid HTTP request");
    }

    HttpRequest request;
    request.body = raw.substr(headerEnd + 4);

    std::istringstream stream(raw.substr(0, headerEnd));
    std::string line;
    if (!std::getline(stream, line)) {
        throw std::runtime_error("missing request line");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream requestLine(line);
    requestLine >> request.method >> request.target;
    if (request.method.empty() || request.target.empty()) {
        throw std::runtime_error("invalid request line");
    }

    const std::size_t question = request.target.find('?');
    request.path = question == std::string::npos ? request.target : request.target.substr(0, question);
    request.query = question == std::string::npos ? "" : request.target.substr(question + 1);

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        request.headers[lowerCopy(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return request;
}

std::string statusMessage(int statusCode) {
    switch (statusCode) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

void writeAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(n);
    }
}

void sendResponse(int clientFd, int statusCode, const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << ' ' << statusMessage(statusCode) << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    writeAll(clientFd, response.str());
}

void handleClient(int clientFd, const SimulationConfig& baseConfig) {
    try {
        const HttpRequest request = parseRequest(readRawRequest(clientFd));

        if (request.method == "OPTIONS") {
            sendResponse(clientFd, 204, "text/plain", "");
            ::close(clientFd);
            return;
        }

        if (request.method == "GET" && (request.path == "/health/live" || request.path == "/health/ready")) {
            sendResponse(clientFd, 200, "application/json", "{\"status\":\"ok\"}");
            ::close(clientFd);
            return;
        }

        const bool isSimulationPath =
            request.path == "/simulate" ||
            request.path == "/api/mega-sena-simulator/simulate";
        if ((request.method == "POST" || request.method == "GET") && isSimulationPath) {
            const auto start = std::chrono::steady_clock::now();
            SimulationConfig config = configFromRequest(baseConfig, request);
            LotteryMachine machine(config);
            machine.initialize();
            machine.run();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            sendResponse(clientFd, 200, "application/json", simulationResponseJson(config, machine, elapsedMs));
            ::close(clientFd);
            return;
        }

        sendResponse(clientFd, 404, "application/json", "{\"error\":\"not found\"}");
    } catch (const std::exception& ex) {
        sendResponse(clientFd, 500, "application/json", std::string("{\"error\":\"") + jsonEscape(ex.what()) + "\"}");
    }
    ::close(clientFd);
}
#endif
}

HttpSimulationServer::HttpSimulationServer(SimulationConfig baseConfig, int port)
    : baseConfig_(std::move(baseConfig)), port_(port) {}

int HttpSimulationServer::run() {
#ifdef _WIN32
    std::cerr << "HTTP service mode is only supported on POSIX targets.\n";
    return 2;
#else
    ::signal(SIGPIPE, SIG_IGN);

    const int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        throw std::runtime_error("failed to create socket");
    }

    int reuse = 1;
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string message = "failed to bind port " + std::to_string(port_) + ": " + std::strerror(errno);
        ::close(serverFd);
        throw std::runtime_error(message);
    }

    if (::listen(serverFd, 32) < 0) {
        const std::string message = std::string("failed to listen: ") + std::strerror(errno);
        ::close(serverFd);
        throw std::runtime_error(message);
    }

    std::cout << "Mega-Sena simulator HTTP service listening on port " << port_ << '\n';
    for (;;) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            continue;
        }

        std::thread(handleClient, clientFd, baseConfig_).detach();
    }
#endif
}
