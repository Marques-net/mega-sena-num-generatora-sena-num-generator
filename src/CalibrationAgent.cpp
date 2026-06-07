#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "LotteryMachine.hpp"
#include "SimulationConfig.hpp"

namespace {
constexpr const char* kAgentName = "mega-sena-dem-calibration-agent";
constexpr const char* kAgentVersion = "0.1.0";

struct Contest {
    int concurso{0};
    std::string dataSorteio;
    std::vector<int> dezenas;
    std::vector<int> ordemSorteio;
};

struct SimulationResult {
    std::vector<int> extracted;
    std::vector<int> sorted;
    double finalTime{0.0};
    bool complete{false};
};

struct StoredState {
    bool exists{false};
    bool matched{false};
    unsigned long long attempts{0};
};

struct ExecResult {
    int exitCode{0};
    std::string output;
};

struct Options {
    int latestLimit{10};
    unsigned long long maxAttemptsPerContest{0};
    unsigned long long checkpointEvery{100};
    std::uint64_t baseSeed{5489};

    std::string mongoUri;
    std::string database{"geek_hub"};
    std::string historyCollection{"mega_sena_resultados"};
    std::string calibrationCollection{"mega_sena_simulacoes"};
    std::string historyFile;
    std::string outputDir{"calibration-output"};
    std::string tempDir{"/tmp"};

    bool dryRun{false};
    bool force{false};
    bool matchOrder{false};
    bool fixedMinMixTime{false};
    bool fixedCaptureMode{false};

    SimulationConfig baseConfig;
};

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n\n"
        << "Calibrates Mega-Sena DEM simulation parameters against historical draws.\n"
        << "By default it processes the 10 latest draws and runs until each draw is matched.\n\n"
        << "Options:\n"
        << "  --mongo-uri <uri>                 MongoDB URI. Env MONGO_URI is also accepted.\n"
        << "  --database <name>                 MongoDB database (default: geek_hub)\n"
        << "  --history-collection <name>       Historical draws collection (default: mega_sena_resultados)\n"
        << "  --calibration-collection <name>   Output collection (default: mega_sena_simulacoes)\n"
        << "  --latest <n>                      Number of latest contests to process (default: 10)\n"
        << "  --history-file <path>             Load draw JSON from file instead of MongoDB\n"
        << "  --output-dir <path>               Artifacts base dir for matched runs\n"
        << "  --max-attempts-per-contest <n>    Stop after n attempts; 0 means unlimited (default: 0)\n"
        << "  --checkpoint-every <n>            Persist progress every n attempts (default: 100)\n"
        << "  --base-seed <n>                   Base seed for deterministic attempt generation\n"
        << "  --match-order                     Match extraction order instead of sorted dozens\n"
        << "  --force                           Reprocess contests already marked as matched\n"
        << "  --dry-run                         Read and simulate without writing MongoDB documents\n"
        << "  --dt <seconds>                    Simulation timestep for calibration (default: 0.001)\n"
        << "  --max-time <seconds>              Maximum simulated time per attempt (default: 30)\n"
        << "  --min-mix-time <seconds>          Fix minimum mix time instead of varying it\n"
        << "  --capture-mode <mode>             Fix capture mode: outlet or top-side\n"
        << "  --mongosh-temp-dir <path>         Directory for temporary mongosh scripts\n"
        << "  --help                            Show this help\n";
}

std::string requireValue(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + argv[index]);
    }
    ++index;
    return argv[index];
}

double parseDouble(const std::string& raw, const std::string& optionName) {
    char* end = nullptr;
    const double value = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0') {
        throw std::runtime_error("invalid numeric value for " + optionName + ": " + raw);
    }
    return value;
}

unsigned long long parseULL(const std::string& raw, const std::string& optionName) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0') {
        throw std::runtime_error("invalid integer value for " + optionName + ": " + raw);
    }
    return value;
}

int parsePositiveInt(const std::string& raw, const std::string& optionName) {
    const unsigned long long value = parseULL(raw, optionName);
    if (value == 0 || value > 1000000ULL) {
        throw std::runtime_error("invalid positive integer value for " + optionName + ": " + raw);
    }
    return static_cast<int>(value);
}

std::string envOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : std::string(value);
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

std::string jsonString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
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

std::string nowIsoUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string compactTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(ms);
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

ExecResult executeCommand(const std::string& command) {
    ExecResult result;
#ifdef _WIN32
    throw std::runtime_error("mongosh integration is supported on POSIX targets only");
#else
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("failed to start command");
    }
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }
    const int status = ::pclose(pipe);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else {
        result.exitCode = status;
    }
    return result;
#endif
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    out << content;
}

std::string extractJsonPayload(const std::string& output, char open, char close) {
    const std::size_t begin = output.find(open);
    const std::size_t end = output.rfind(close);
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        return trim(output);
    }
    return output.substr(begin, end - begin + 1);
}

std::vector<std::string> jsonObjectsFromArray(const std::string& json) {
    std::vector<std::string> objects;
    bool inString = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string::npos;

    for (std::size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

std::optional<std::string> extractRawField(const std::string& object, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    const std::size_t key = object.find(needle);
    if (key == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = object.find(':', key + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t begin = colon + 1;
    while (begin < object.size() && std::isspace(static_cast<unsigned char>(object[begin]))) {
        ++begin;
    }
    if (begin >= object.size()) {
        return std::nullopt;
    }

    if (object[begin] == '"') {
        bool escaped = false;
        for (std::size_t i = begin + 1; i < object.size(); ++i) {
            if (escaped) {
                escaped = false;
            } else if (object[i] == '\\') {
                escaped = true;
            } else if (object[i] == '"') {
                return object.substr(begin, i - begin + 1);
            }
        }
        return std::nullopt;
    }
    if (object[begin] == '[') {
        int depth = 0;
        for (std::size_t i = begin; i < object.size(); ++i) {
            if (object[i] == '[') {
                ++depth;
            } else if (object[i] == ']') {
                --depth;
                if (depth == 0) {
                    return object.substr(begin, i - begin + 1);
                }
            }
        }
        return std::nullopt;
    }

    std::size_t end = begin;
    while (end < object.size() && object[end] != ',' && object[end] != '}') {
        ++end;
    }
    return trim(object.substr(begin, end - begin));
}

int extractIntField(const std::string& object, const std::string& field, int fallback = 0) {
    const auto raw = extractRawField(object, field);
    if (!raw) {
        return fallback;
    }
    return static_cast<int>(std::strtol(raw->c_str(), nullptr, 10));
}

unsigned long long extractULLField(const std::string& object, const std::string& field, unsigned long long fallback = 0) {
    const auto raw = extractRawField(object, field);
    if (!raw) {
        return fallback;
    }
    if (raw->size() >= 2 && raw->front() == '"' && raw->back() == '"') {
        const std::string value = raw->substr(1, raw->size() - 2);
        return std::strtoull(value.c_str(), nullptr, 10);
    }
    return std::strtoull(raw->c_str(), nullptr, 10);
}

bool extractBoolField(const std::string& object, const std::string& field, bool fallback = false) {
    const auto raw = extractRawField(object, field);
    if (!raw) {
        return fallback;
    }
    return *raw == "true";
}

std::string extractStringField(const std::string& object, const std::string& field) {
    const auto raw = extractRawField(object, field);
    if (!raw || raw->size() < 2 || raw->front() != '"' || raw->back() != '"') {
        return "";
    }
    std::string value;
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < raw->size(); ++i) {
        const char c = (*raw)[i];
        if (escaped) {
            switch (c) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(c);
                break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            value.push_back(c);
        }
    }
    return value;
}

std::vector<int> extractIntArrayField(const std::string& object, const std::string& field) {
    std::vector<int> values;
    const auto raw = extractRawField(object, field);
    if (!raw || raw->empty() || raw->front() != '[') {
        return values;
    }
    std::stringstream stream(raw->substr(1, raw->size() - 2));
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            values.push_back(static_cast<int>(std::strtol(item.c_str(), nullptr, 10)));
        }
    }
    return values;
}

std::vector<Contest> parseContestsJson(const std::string& jsonPayload) {
    std::vector<Contest> contests;
    for (const std::string& object : jsonObjectsFromArray(jsonPayload)) {
        Contest contest;
        contest.concurso = extractIntField(object, "concurso");
        contest.dataSorteio = extractStringField(object, "dataSorteio");
        contest.dezenas = extractIntArrayField(object, "dezenas");
        contest.ordemSorteio = extractIntArrayField(object, "ordemSorteio");
        std::sort(contest.dezenas.begin(), contest.dezenas.end());
        if (contest.concurso > 0 && contest.dezenas.size() == 6) {
            contests.push_back(std::move(contest));
        }
    }
    std::sort(contests.begin(), contests.end(), [](const Contest& left, const Contest& right) {
        return left.concurso > right.concurso;
    });
    return contests;
}

std::uint64_t splitMix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

template <typename T>
const T& pickMixedRadix(std::uint64_t& index, const std::vector<T>& values) {
    const T& value = values[static_cast<std::size_t>(index % values.size())];
    index /= values.size();
    return value;
}

std::vector<double> minMixProfiles(const Options& options) {
    if (options.fixedMinMixTime) {
        return {options.baseConfig.minMixTime};
    }
    const std::vector<double> candidates{1.5, 3.0, 6.0, 10.0};
    std::vector<double> values;
    for (double value : candidates) {
        if (value < options.baseConfig.maxDuration - 0.25) {
            values.push_back(value);
        }
    }
    if (values.empty()) {
        values.push_back(std::max(0.1, options.baseConfig.maxDuration * 0.25));
    }
    return values;
}

std::vector<std::string> captureModeProfiles(const Options& options) {
    if (options.fixedCaptureMode) {
        return {options.baseConfig.captureMode};
    }
    return {"outlet", "top-side"};
}

SimulationConfig configForAttempt(const Options& options, int concurso, unsigned long long attempt) {
    const std::vector<double> jets{0.85, 1.05, 1.25, 1.45, 1.75, 2.10};
    const std::vector<double> turbulences{0.18, 0.28, 0.38, 0.55, 0.80};
    const std::vector<double> restitutions{0.78, 0.85, 0.92};
    const std::vector<double> frictions{0.12, 0.20, 0.32};
    const std::vector<double> captureRadii{0.045, 0.055, 0.070};
    const std::vector<double> captureSpeeds{2.50, 4.00, 6.00};
    const std::vector<double> damping{1.8, 2.5, 3.2};
    const std::vector<double> tangentialDamping{0.20, 0.35, 0.50};
    const std::vector<double> minMixTimes = minMixProfiles(options);
    const std::vector<std::string> captureModes = captureModeProfiles(options);

    std::uint64_t profile = attempt - 1ULL;
    SimulationConfig config = options.baseConfig;
    config.upwardJetForce = pickMixedRadix(profile, jets);
    config.turbulenceForce = pickMixedRadix(profile, turbulences);
    config.restitution = pickMixedRadix(profile, restitutions);
    config.friction = pickMixedRadix(profile, frictions);
    config.captureRadius = pickMixedRadix(profile, captureRadii);
    config.captureMaxSpeed = pickMixedRadix(profile, captureSpeeds);
    config.normalDamping = pickMixedRadix(profile, damping);
    config.tangentialDamping = pickMixedRadix(profile, tangentialDamping);
    config.minMixTime = pickMixedRadix(profile, minMixTimes);
    config.captureMode = pickMixedRadix(profile, captureModes);
    config.captureRequiresDownwardVelocity = config.captureMode == "outlet";

    const std::uint64_t seedMaterial =
        options.baseSeed ^
        (static_cast<std::uint64_t>(concurso) * 0xd6e8feb86659fd93ULL) ^
        (attempt * 0xa0761d6478bd642fULL) ^
        (profile * 0xe7037ed1a0b428dbULL);
    config.seed = splitMix64(seedMaterial);
    config.writeArtifacts = false;
    config.outputDir = options.outputDir;
    return config;
}

SimulationResult runSimulation(SimulationConfig config) {
    config.writeArtifacts = false;
    LotteryMachine machine(config);
    machine.initialize();
    machine.run();

    SimulationResult result;
    result.extracted = machine.extractedBalls();
    result.sorted = result.extracted;
    std::sort(result.sorted.begin(), result.sorted.end());
    result.finalTime = machine.finalTime();
    result.complete = static_cast<int>(result.extracted.size()) == config.ballsToExtract;
    return result;
}

void writeMatchedArtifacts(SimulationConfig config, const std::filesystem::path& outputDir) {
    config.outputDir = outputDir.string();
    config.writeArtifacts = true;
    LotteryMachine machine(config);
    machine.initialize();
    machine.run();
}

bool matchesContest(const Contest& contest, const SimulationResult& result, bool matchOrder) {
    if (!result.complete) {
        return false;
    }
    if (matchOrder && contest.ordemSorteio.size() == result.extracted.size()) {
        return contest.ordemSorteio == result.extracted;
    }
    return contest.dezenas == result.sorted;
}

std::string paramsToJson(const SimulationConfig& config) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{"
        << "\"seed\":" << jsonString(std::to_string(config.seed)) << ','
        << "\"seedString\":" << jsonString(std::to_string(config.seed)) << ','
        << "\"timeStep\":" << config.timeStep << ','
        << "\"maxDuration\":" << config.maxDuration << ','
        << "\"minMixTime\":" << config.minMixTime << ','
        << "\"ballMass\":" << config.ballMass << ','
        << "\"ballRadius\":" << config.ballRadius << ','
        << "\"restitution\":" << config.restitution << ','
        << "\"friction\":" << config.friction << ','
        << "\"upwardJetForce\":" << config.upwardJetForce << ','
        << "\"turbulenceForce\":" << config.turbulenceForce << ','
        << "\"captureMode\":" << jsonString(config.captureMode) << ','
        << "\"captureRadius\":" << config.captureRadius << ','
        << "\"captureMaxSpeed\":" << config.captureMaxSpeed << ','
        << "\"captureRequiresDownwardVelocity\":" << (config.captureRequiresDownwardVelocity ? "true" : "false") << ','
        << "\"normalStiffness\":" << config.normalStiffness << ','
        << "\"normalDamping\":" << config.normalDamping << ','
        << "\"tangentialDamping\":" << config.tangentialDamping
        << "}";
    return out.str();
}

std::string simulatorResultToJson(const SimulationResult& result) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{"
        << "\"numbers\":" << vectorToJson(result.extracted) << ','
        << "\"sortedNumbers\":" << vectorToJson(result.sorted) << ','
        << "\"complete\":" << (result.complete ? "true" : "false") << ','
        << "\"finalTime\":" << result.finalTime
        << "}";
    return out.str();
}

std::string currentGitCommit() {
    try {
        const ExecResult result = executeCommand("git rev-parse --short HEAD 2>/dev/null");
        if (result.exitCode == 0) {
            return trim(result.output);
        }
    } catch (const std::exception&) {
    }
    return "unknown";
}

std::string calibrationDocumentJson(
    const Contest& contest,
    const SimulationResult& result,
    const SimulationConfig& config,
    unsigned long long attempts,
    const std::string& status,
    const std::string& artifactOutputDir,
    const std::string& gitCommit
) {
    const bool matched = status == "matched";
    std::ostringstream out;
    out << "{"
        << "\"documentType\":\"dem_parameter_calibration\","
        << "\"agentName\":" << jsonString(kAgentName) << ','
        << "\"agentVersion\":" << jsonString(kAgentVersion) << ','
        << "\"status\":" << jsonString(status) << ','
        << "\"matched\":" << (matched ? "true" : "false") << ','
        << "\"concurso\":" << contest.concurso << ','
        << "\"dataSorteio\":" << jsonString(contest.dataSorteio) << ','
        << "\"targetNumbers\":" << vectorToJson(contest.dezenas) << ','
        << "\"targetOrder\":" << vectorToJson(contest.ordemSorteio) << ','
        << "\"attempts\":" << jsonString(std::to_string(attempts)) << ','
        << "\"attemptsString\":" << jsonString(std::to_string(attempts)) << ','
        << "\"parameters\":" << paramsToJson(config) << ','
        << "\"simulatorResult\":" << simulatorResultToJson(result) << ','
        << "\"artifactOutputDir\":" << jsonString(artifactOutputDir) << ','
        << "\"simulator\":{"
        << "\"name\":\"Mega-Sena DEM simulator\","
        << "\"gitCommit\":" << jsonString(gitCommit)
        << "},"
        << "\"updatedAt\":" << jsonString(nowIsoUtc());
    if (matched) {
        out << ",\"matchedAt\":" << jsonString(nowIsoUtc());
    }
    out << "}";
    return out.str();
}

std::string mongoScriptPreamble(const Options& options) {
    return "const databaseName = " + jsonString(options.database) + ";\n"
           "const dbh = db.getSiblingDB(databaseName);\n";
}

std::string runMongoScript(const Options& options, const std::string& scriptBody) {
    if (options.mongoUri.empty()) {
        throw std::runtime_error("MongoDB URI is required. Set MONGO_URI or pass --mongo-uri.");
    }
    const std::filesystem::path scriptPath =
        std::filesystem::path(options.tempDir) /
        ("mega-sena-calibration-" + compactTimestamp() + "-" + std::to_string(::getpid()) + ".js");
    writeFile(scriptPath, mongoScriptPreamble(options) + scriptBody);
    const std::string command = "mongosh " + shellQuote(options.mongoUri) + " --quiet " + shellQuote(scriptPath.string()) + " 2>&1";
    const ExecResult result = executeCommand(command);
    std::error_code ec;
    std::filesystem::remove(scriptPath, ec);
    if (result.exitCode != 0) {
        throw std::runtime_error("mongosh failed: " + trim(result.output));
    }
    return result.output;
}

std::vector<Contest> loadLatestDrawsFromMongo(const Options& options) {
    const std::string script =
        "const rows = dbh.getCollection(" + jsonString(options.historyCollection) + ")\n"
        "  .find({ dezenas: { $type: 'array' } })\n"
        "  .sort({ concurso: -1 })\n"
        "  .limit(" + std::to_string(options.latestLimit) + ")\n"
        "  .map(d => ({ concurso: d.concurso, dataSorteio: d.dataSorteio, dezenas: d.dezenas, ordemSorteio: d.ordemSorteio || [] }))\n"
        "  .toArray();\n"
        "print(JSON.stringify(rows));\n";
    return parseContestsJson(extractJsonPayload(runMongoScript(options, script), '[', ']'));
}

std::vector<Contest> loadLatestDraws(const Options& options) {
    if (!options.historyFile.empty()) {
        std::vector<Contest> contests = parseContestsJson(extractJsonPayload(readFile(options.historyFile), '[', ']'));
        if (static_cast<int>(contests.size()) > options.latestLimit) {
            contests.resize(static_cast<std::size_t>(options.latestLimit));
        }
        return contests;
    }
    return loadLatestDrawsFromMongo(options);
}

void ensureMongoIndexes(const Options& options) {
    if (options.dryRun) {
        return;
    }
    const std::string script =
        "const col = dbh.getCollection(" + jsonString(options.calibrationCollection) + ");\n"
        "col.createIndex({ agentName: 1, concurso: 1 }, { unique: true, name: 'agent_concurso_unique' });\n"
        "col.createIndex({ status: 1, concurso: -1 }, { name: 'status_concurso' });\n"
        "print(JSON.stringify({ ok: true }));\n";
    runMongoScript(options, script);
}

StoredState readStoredState(const Options& options, int concurso) {
    if (options.force || options.dryRun) {
        return {};
    }
    const std::string script =
        "const doc = dbh.getCollection(" + jsonString(options.calibrationCollection) + ")\n"
        "  .findOne({ agentName: " + jsonString(kAgentName) + ", concurso: " + std::to_string(concurso) + " });\n"
        "print(JSON.stringify(doc ? { status: doc.status, attempts: doc.attempts ? doc.attempts.toString() : (doc.attemptsString || '0'), matched: doc.status === 'matched' } : null));\n";
    const std::string payload = trim(runMongoScript(options, script));
    if (payload == "null" || payload.empty()) {
        return {};
    }
    const std::string object = extractJsonPayload(payload, '{', '}');
    StoredState state;
    state.exists = true;
    state.matched = extractBoolField(object, "matched");
    state.attempts = extractULLField(object, "attempts", 0);
    return state;
}

void persistCalibrationDocument(const Options& options, const std::string& documentJson) {
    if (options.dryRun) {
        std::cout << "[dry-run] would persist: " << documentJson << "\n";
        return;
    }
    const std::string script =
        "const payload = JSON.parse(" + jsonString(documentJson) + ");\n"
        "payload.attemptsString = payload.attemptsString || payload.attempts;\n"
        "payload.attempts = Long.fromString(payload.attemptsString);\n"
        "payload.parameters.seedString = payload.parameters.seedString || payload.parameters.seed;\n"
        "payload.parameters.seed = payload.parameters.seedString;\n"
        "const col = dbh.getCollection(" + jsonString(options.calibrationCollection) + ");\n"
        "col.updateOne(\n"
        "  { agentName: payload.agentName, concurso: payload.concurso },\n"
        "  { $set: payload, $setOnInsert: { createdAt: payload.updatedAt } },\n"
        "  { upsert: true }\n"
        ");\n"
        "print(JSON.stringify({ ok: true, concurso: payload.concurso, status: payload.status, attempts: payload.attempts }));\n";
    runMongoScript(options, script);
}

std::string redactedUri(const std::string& uri) {
    const std::size_t scheme = uri.find("://");
    const std::size_t at = uri.find('@');
    if (scheme == std::string::npos || at == std::string::npos || at < scheme) {
        return uri;
    }
    return uri.substr(0, scheme + 3) + "[credentials-redacted]" + uri.substr(at);
}

Options parseArgs(int argc, char** argv) {
    Options options;
    options.mongoUri = envOrEmpty("MONGO_URI");
    options.baseConfig.timeStep = 0.001;
    options.baseConfig.maxDuration = 30.0;
    options.baseConfig.minMixTime = 3.0;
    options.baseConfig.csvInterval = 1.0;
    options.baseConfig.writeArtifacts = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--mongo-uri") {
            options.mongoUri = requireValue(i, argc, argv);
        } else if (arg == "--database") {
            options.database = requireValue(i, argc, argv);
        } else if (arg == "--history-collection") {
            options.historyCollection = requireValue(i, argc, argv);
        } else if (arg == "--calibration-collection") {
            options.calibrationCollection = requireValue(i, argc, argv);
        } else if (arg == "--latest") {
            options.latestLimit = parsePositiveInt(requireValue(i, argc, argv), arg);
        } else if (arg == "--history-file") {
            options.historyFile = requireValue(i, argc, argv);
        } else if (arg == "--output-dir") {
            options.outputDir = requireValue(i, argc, argv);
        } else if (arg == "--max-attempts-per-contest") {
            options.maxAttemptsPerContest = parseULL(requireValue(i, argc, argv), arg);
        } else if (arg == "--checkpoint-every") {
            options.checkpointEvery = parseULL(requireValue(i, argc, argv), arg);
            if (options.checkpointEvery == 0) {
                throw std::runtime_error("--checkpoint-every must be greater than zero");
            }
        } else if (arg == "--base-seed") {
            options.baseSeed = static_cast<std::uint64_t>(parseULL(requireValue(i, argc, argv), arg));
        } else if (arg == "--match-order") {
            options.matchOrder = true;
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--dry-run") {
            options.dryRun = true;
        } else if (arg == "--dt") {
            options.baseConfig.timeStep = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--max-time") {
            options.baseConfig.maxDuration = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--min-mix-time") {
            options.baseConfig.minMixTime = parseDouble(requireValue(i, argc, argv), arg);
            options.fixedMinMixTime = true;
        } else if (arg == "--capture-mode") {
            options.baseConfig.captureMode = requireValue(i, argc, argv);
            if (options.baseConfig.captureMode != "outlet" && options.baseConfig.captureMode != "top-side") {
                throw std::runtime_error("capture mode must be outlet or top-side");
            }
            options.fixedCaptureMode = true;
        } else if (arg == "--mongosh-temp-dir") {
            options.tempDir = requireValue(i, argc, argv);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.latestLimit <= 0) {
        throw std::runtime_error("--latest must be greater than zero");
    }
    if (options.baseConfig.timeStep <= 0.0 || options.baseConfig.maxDuration <= 0.0) {
        throw std::runtime_error("simulation time parameters must be positive");
    }
    if (options.fixedMinMixTime && options.baseConfig.minMixTime >= options.baseConfig.maxDuration) {
        throw std::runtime_error("--min-mix-time must be lower than --max-time");
    }
    if (options.mongoUri.empty() && options.historyFile.empty()) {
        options.mongoUri = "mongodb://127.0.0.1:27017/geek_hub";
    }
    return options;
}

int runCalibration(const Options& options) {
    std::cout << "Mega-Sena DEM calibration agent\n";
    std::cout << "agent=" << kAgentName << " version=" << kAgentVersion << "\n";
    if (!options.historyFile.empty()) {
        std::cout << "history=file:" << options.historyFile << "\n";
    } else {
        std::cout << "mongo=" << redactedUri(options.mongoUri)
                  << " database=" << options.database
                  << " historyCollection=" << options.historyCollection
                  << " calibrationCollection=" << options.calibrationCollection << "\n";
    }
    std::cout << "latest=" << options.latestLimit
              << " maxAttemptsPerContest=" << options.maxAttemptsPerContest
              << " dryRun=" << (options.dryRun ? "yes" : "no") << "\n";

    ensureMongoIndexes(options);
    const std::vector<Contest> contests = loadLatestDraws(options);
    if (contests.empty()) {
        throw std::runtime_error("no historical Mega-Sena contests found");
    }

    const std::string gitCommit = currentGitCommit();
    int matchedContests = 0;
    int skippedContests = 0;

    for (const Contest& contest : contests) {
        const StoredState stored = readStoredState(options, contest.concurso);
        if (stored.matched && !options.force) {
            ++skippedContests;
            std::cout << "contest " << contest.concurso << " already matched, skipping\n";
            continue;
        }

        const unsigned long long startAttempt = stored.exists && stored.attempts > 0 ? stored.attempts + 1ULL : 1ULL;
        std::cout << "contest " << contest.concurso
                  << " target=" << vectorToJson(contest.dezenas)
                  << " startAttempt=" << startAttempt << "\n";

        bool matched = false;
        unsigned long long attemptsDoneForContest = 0;
        for (unsigned long long attempt = startAttempt;; ++attempt) {
            ++attemptsDoneForContest;
            SimulationConfig config = configForAttempt(options, contest.concurso, attempt);
            SimulationResult result = runSimulation(config);
            const bool isMatch = matchesContest(contest, result, options.matchOrder);

            if (isMatch) {
                const std::filesystem::path artifactDir =
                    std::filesystem::path(options.outputDir) /
                    ("concurso-" + std::to_string(contest.concurso) + "-attempt-" + std::to_string(attempt));
                writeMatchedArtifacts(config, artifactDir);
                const std::string doc = calibrationDocumentJson(
                    contest,
                    result,
                    config,
                    attempt,
                    "matched",
                    artifactDir.string(),
                    gitCommit
                );
                persistCalibrationDocument(options, doc);
                ++matchedContests;
                matched = true;
                std::cout << "contest " << contest.concurso
                          << " matched at attempt " << attempt
                          << " result=" << vectorToJson(result.sorted)
                          << " artifacts=" << artifactDir << "\n";
                break;
            }

            const bool hitAttemptLimit =
                options.maxAttemptsPerContest > 0 && attemptsDoneForContest >= options.maxAttemptsPerContest;
            if (attempt == startAttempt || attempt % options.checkpointEvery == 0 || hitAttemptLimit) {
                const std::string status = hitAttemptLimit ? "attempt_limit" : "running";
                const std::string doc = calibrationDocumentJson(
                    contest,
                    result,
                    config,
                    attempt,
                    status,
                    "",
                    gitCommit
                );
                persistCalibrationDocument(options, doc);
                std::cout << "contest " << contest.concurso
                          << " checkpoint attempt=" << attempt
                          << " status=" << status
                          << " result=" << vectorToJson(result.sorted)
                          << "\n";
            }
            if (hitAttemptLimit) {
                break;
            }
        }

        if (!matched && options.maxAttemptsPerContest == 0) {
            throw std::runtime_error("internal error: unlimited loop exited without match");
        }
    }

    std::cout << "calibration pass finished: matched=" << matchedContests
              << " skipped=" << skippedContests
              << " processed=" << contests.size() << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        return runCalibration(options);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
