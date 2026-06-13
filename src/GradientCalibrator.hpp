#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SimulationConfig.hpp"

struct GradientCalibrationTelemetry {
    double loss{0.0};
    double surrogateLoss{0.0};
    double learningRate{0.0};
    double surrogateLearningRate{0.0};
    double explorationScale{0.0};
    int matchedNumbers{0};
    bool complete{false};
    unsigned long long cycle{0};
    std::vector<double> errorVector;
    std::vector<double> gradient;
    std::vector<std::string> parameterNames;
    std::vector<double> parameterValues;
};

class GradientCalibrator {
public:
    struct Options {
        double parameterLearningRate{0.08};
        double surrogateLearningRate{0.04};
        double explorationScale{0.18};
        bool fixedMinMixTime{false};
        bool fixedCaptureMode{false};
        int workerIndex{0};
        int workerCount{1};
    };

    GradientCalibrator(
        SimulationConfig baseConfig,
        std::vector<int> targetNumbers,
        std::uint64_t baseSeed,
        int contest,
        Options options
    );

    SimulationConfig propose(unsigned long long attempt);
    GradientCalibrationTelemetry observe(const std::vector<int>& sortedResult, bool complete);

private:
    struct ParameterSpec {
        std::string name;
        double minValue{0.0};
        double maxValue{1.0};
        bool locked{false};
    };

    struct ForwardPass {
        std::vector<double> input;
        std::vector<double> hiddenRaw;
        std::vector<double> hidden;
        std::vector<double> outputRaw;
        std::vector<double> output;
    };

    SimulationConfig baseConfig_;
    std::vector<int> targetNumbers_;
    std::vector<double> targetNormalized_;
    std::uint64_t baseSeed_{0};
    int contest_{0};
    Options options_;

    std::vector<ParameterSpec> specs_;
    std::vector<double> parameters_;
    std::vector<double> lastCandidate_;
    std::vector<double> lastInput_;

    int hiddenSize_{16};
    std::vector<std::vector<double>> w1_;
    std::vector<double> b1_;
    std::vector<std::vector<double>> w2_;
    std::vector<double> b2_;
    unsigned long long cycle_{0};

    void initializeSpecs();
    void initializeNetwork();
    std::vector<double> buildInput(const std::vector<double>& candidate) const;
    ForwardPass forward(const std::vector<double>& input) const;
    double trainSurrogate(const std::vector<double>& input, const std::vector<double>& observed);
    std::vector<double> gradientTowardTarget(const std::vector<double>& input) const;
    std::vector<double> normalizeResult(const std::vector<int>& sortedResult, bool complete) const;
    double resultLoss(const std::vector<double>& observed, bool complete) const;
    int countMatches(const std::vector<int>& sortedResult) const;
    void applyCandidateToConfig(const std::vector<double>& candidate, SimulationConfig& config) const;
    std::vector<double> denormalizedParameters() const;
};
