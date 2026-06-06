#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "HttpSimulationServer.hpp"
#include "LotteryMachine.hpp"
#include "SimulationConfig.hpp"

namespace {
struct ProgramOptions {
    SimulationConfig config;
    bool serve{false};
    int port{8080};
};

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n\n"
        << "Options:\n"
        << "  --serve                 Run HTTP API service instead of one CLI simulation\n"
        << "  --port <n>              HTTP API service port (default: 8080)\n"
        << "  --seed <n>              Random seed (default: 5489)\n"
        << "  --output-dir <path>     Output directory (default: output)\n"
        << "  --dt <seconds>          Timestep (default: 0.0005)\n"
        << "  --max-time <seconds>    Maximum simulated time (default: 120)\n"
        << "  --min-mix-time <sec>    Minimum mixing time before capture (default: 10)\n"
        << "  --csv-interval <sec>    CSV sampling interval (default: 0.02)\n"
        << "  --jet <newtons>         Upward jet force scale (default: 1.05)\n"
        << "  --turbulence <newtons>  Turbulence force scale (default: 0.28)\n"
        << "  --capture-mode <mode>   outlet or top-side (default: outlet)\n"
        << "  --help                  Show this help\n";
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

std::uint64_t parseUInt64(const std::string& raw, const std::string& optionName) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0') {
        throw std::runtime_error("invalid integer value for " + optionName + ": " + raw);
    }
    return static_cast<std::uint64_t>(value);
}

int parseInt(const std::string& raw, const std::string& optionName) {
    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0' || value <= 0 || value > 65535) {
        throw std::runtime_error("invalid integer value for " + optionName + ": " + raw);
    }
    return static_cast<int>(value);
}

ProgramOptions parseArgs(int argc, char** argv) {
    ProgramOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "--serve") {
            options.serve = true;
        } else if (arg == "--port") {
            options.port = parseInt(requireValue(i, argc, argv), arg);
        } else if (arg == "--seed") {
            options.config.seed = parseUInt64(requireValue(i, argc, argv), arg);
        } else if (arg == "--output-dir") {
            options.config.outputDir = requireValue(i, argc, argv);
        } else if (arg == "--dt") {
            options.config.timeStep = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--max-time") {
            options.config.maxDuration = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--min-mix-time") {
            options.config.minMixTime = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--csv-interval") {
            options.config.csvInterval = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--jet") {
            options.config.upwardJetForce = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--turbulence") {
            options.config.turbulenceForce = parseDouble(requireValue(i, argc, argv), arg);
        } else if (arg == "--capture-mode") {
            options.config.captureMode = requireValue(i, argc, argv);
            if (options.config.captureMode != "outlet" && options.config.captureMode != "top-side") {
                throw std::runtime_error("capture mode must be outlet or top-side");
            }
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.config.timeStep <= 0.0 || options.config.maxDuration <= 0.0 || options.config.csvInterval <= 0.0) {
        throw std::runtime_error("time parameters must be positive");
    }
    if (options.config.ballCount <= 0 || options.config.ballsToExtract <= 0 ||
        options.config.ballsToExtract > options.config.ballCount) {
        throw std::runtime_error("invalid ball count or extraction count");
    }
    return options;
}
}

int main(int argc, char** argv) {
    try {
        ProgramOptions options = parseArgs(argc, argv);
        if (options.serve) {
            HttpSimulationServer server(options.config, options.port);
            return server.run();
        }

        SimulationConfig config = options.config;
        LotteryMachine machine(config);
        machine.initialize();
        machine.run();

        std::cout << "Simulation finished at t=" << machine.finalTime() << " s\n";
        std::cout << "Extracted:";
        for (int id : machine.extractedBalls()) {
            std::cout << ' ' << id;
        }
        std::cout << "\nCSV: " << config.outputDir << "/trajectory.csv\n";
        std::cout << "Result: " << config.outputDir << "/result.txt\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
