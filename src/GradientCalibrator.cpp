#include "GradientCalibrator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace {
constexpr int kOutputSize = 6;

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double sigmoid(double value) {
    if (value >= 0.0) {
        const double z = std::exp(-value);
        return 1.0 / (1.0 + z);
    }
    const double z = std::exp(value);
    return z / (1.0 + z);
}

double normalizeNumber(int number) {
    return std::clamp((static_cast<double>(number) - 1.0) / 59.0, 0.0, 1.0);
}

std::uint64_t splitMix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double unitFromBits(std::uint64_t value) {
    return static_cast<double>(value >> 11U) * (1.0 / 9007199254740992.0);
}

double signedNoise(std::uint64_t seed) {
    return unitFromBits(splitMix64(seed)) * 2.0 - 1.0;
}

double normalizeParameter(double value, double minValue, double maxValue) {
    if (maxValue <= minValue) {
        return 0.0;
    }
    return clamp01((value - minValue) / (maxValue - minValue));
}

double denormalizeParameter(double normalized, double minValue, double maxValue) {
    return minValue + clamp01(normalized) * (maxValue - minValue);
}
} // namespace

GradientCalibrator::GradientCalibrator(
    SimulationConfig baseConfig,
    std::vector<int> targetNumbers,
    std::uint64_t baseSeed,
    int contest,
    Options options
) :
    baseConfig_(std::move(baseConfig)),
    targetNumbers_(std::move(targetNumbers)),
    baseSeed_(baseSeed),
    contest_(contest),
    options_(options) {
    if (targetNumbers_.size() != static_cast<std::size_t>(kOutputSize)) {
        throw std::runtime_error("gradient calibrator requires six target numbers");
    }
    std::sort(targetNumbers_.begin(), targetNumbers_.end());
    targetNormalized_.reserve(targetNumbers_.size());
    for (int number : targetNumbers_) {
        targetNormalized_.push_back(normalizeNumber(number));
    }
    initializeSpecs();
    initializeNetwork();
}

void GradientCalibrator::initializeSpecs() {
    const double minMixMax = std::max(0.5, std::min(12.0, baseConfig_.maxDuration - 0.25));
    specs_ = {
        {"upwardJetForce", 0.70, 2.60, false},
        {"turbulenceForce", 0.10, 1.10, false},
        {"restitution", 0.60, 0.98, false},
        {"friction", 0.05, 0.50, false},
        {"captureRadius", 0.035, 0.090, false},
        {"captureMaxSpeed", 1.50, 7.50, false},
        {"normalDamping", 1.20, 4.50, false},
        {"tangentialDamping", 0.10, 0.75, false},
        {"minMixTime", 0.25, minMixMax, options_.fixedMinMixTime},
        {"captureModeBlend", 0.0, 1.0, options_.fixedCaptureMode},
    };

    parameters_.clear();
    parameters_.reserve(specs_.size());
    parameters_.push_back(normalizeParameter(baseConfig_.upwardJetForce, specs_[0].minValue, specs_[0].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.turbulenceForce, specs_[1].minValue, specs_[1].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.restitution, specs_[2].minValue, specs_[2].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.friction, specs_[3].minValue, specs_[3].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.captureRadius, specs_[4].minValue, specs_[4].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.captureMaxSpeed, specs_[5].minValue, specs_[5].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.normalDamping, specs_[6].minValue, specs_[6].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.tangentialDamping, specs_[7].minValue, specs_[7].maxValue));
    parameters_.push_back(normalizeParameter(baseConfig_.minMixTime, specs_[8].minValue, specs_[8].maxValue));
    parameters_.push_back(baseConfig_.captureMode == "top-side" ? 1.0 : 0.0);
}

void GradientCalibrator::initializeNetwork() {
    const int inputSize = static_cast<int>(parameters_.size() + targetNormalized_.size() + 3);
    w1_.assign(static_cast<std::size_t>(hiddenSize_), std::vector<double>(static_cast<std::size_t>(inputSize), 0.0));
    b1_.assign(static_cast<std::size_t>(hiddenSize_), 0.0);
    w2_.assign(static_cast<std::size_t>(kOutputSize), std::vector<double>(static_cast<std::size_t>(hiddenSize_), 0.0));
    b2_.assign(static_cast<std::size_t>(kOutputSize), 0.0);

    std::uint64_t seed = baseSeed_ ^
        (static_cast<std::uint64_t>(contest_) * 0xd6e8feb86659fd93ULL) ^
        (static_cast<std::uint64_t>(options_.workerIndex + 1) * 0xa0761d6478bd642fULL);

    for (int h = 0; h < hiddenSize_; ++h) {
        b1_[static_cast<std::size_t>(h)] = 0.02 * signedNoise(seed + static_cast<std::uint64_t>(h));
        for (int i = 0; i < inputSize; ++i) {
            const std::uint64_t material = seed + 1315423911ULL * static_cast<std::uint64_t>(h + 1) +
                2654435761ULL * static_cast<std::uint64_t>(i + 1);
            w1_[static_cast<std::size_t>(h)][static_cast<std::size_t>(i)] = 0.18 * signedNoise(material);
        }
    }
    for (int o = 0; o < kOutputSize; ++o) {
        b2_[static_cast<std::size_t>(o)] = 0.02 * signedNoise(seed + 9001ULL + static_cast<std::uint64_t>(o));
        for (int h = 0; h < hiddenSize_; ++h) {
            const std::uint64_t material = seed + 97777ULL * static_cast<std::uint64_t>(o + 1) +
                104729ULL * static_cast<std::uint64_t>(h + 1);
            w2_[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] = 0.18 * signedNoise(material);
        }
    }
}

std::vector<double> GradientCalibrator::buildInput(const std::vector<double>& candidate) const {
    std::vector<double> input;
    input.reserve(candidate.size() + targetNormalized_.size() + 3);
    input.insert(input.end(), candidate.begin(), candidate.end());
    input.insert(input.end(), targetNormalized_.begin(), targetNormalized_.end());
    input.push_back(std::clamp(static_cast<double>(contest_) / 4000.0, 0.0, 1.0));
    input.push_back(std::clamp(static_cast<double>(options_.workerIndex) / std::max(1, options_.workerCount), 0.0, 1.0));
    input.push_back(std::clamp(static_cast<double>(cycle_) / 1000000.0, 0.0, 1.0));
    return input;
}

GradientCalibrator::ForwardPass GradientCalibrator::forward(const std::vector<double>& input) const {
    ForwardPass pass;
    pass.input = input;
    pass.hiddenRaw.assign(static_cast<std::size_t>(hiddenSize_), 0.0);
    pass.hidden.assign(static_cast<std::size_t>(hiddenSize_), 0.0);
    pass.outputRaw.assign(static_cast<std::size_t>(kOutputSize), 0.0);
    pass.output.assign(static_cast<std::size_t>(kOutputSize), 0.0);

    for (int h = 0; h < hiddenSize_; ++h) {
        double value = b1_[static_cast<std::size_t>(h)];
        for (std::size_t i = 0; i < input.size(); ++i) {
            value += w1_[static_cast<std::size_t>(h)][i] * input[i];
        }
        pass.hiddenRaw[static_cast<std::size_t>(h)] = value;
        pass.hidden[static_cast<std::size_t>(h)] = std::tanh(value);
    }

    for (int o = 0; o < kOutputSize; ++o) {
        double value = b2_[static_cast<std::size_t>(o)];
        for (int h = 0; h < hiddenSize_; ++h) {
            value += w2_[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] *
                pass.hidden[static_cast<std::size_t>(h)];
        }
        pass.outputRaw[static_cast<std::size_t>(o)] = value;
        pass.output[static_cast<std::size_t>(o)] = sigmoid(value);
    }
    return pass;
}

double GradientCalibrator::trainSurrogate(const std::vector<double>& input, const std::vector<double>& observed) {
    const ForwardPass pass = forward(input);
    std::vector<double> dOutputRaw(static_cast<std::size_t>(kOutputSize), 0.0);
    double loss = 0.0;
    for (int o = 0; o < kOutputSize; ++o) {
        const double error = pass.output[static_cast<std::size_t>(o)] - observed[static_cast<std::size_t>(o)];
        loss += error * error;
        dOutputRaw[static_cast<std::size_t>(o)] =
            (2.0 * error / static_cast<double>(kOutputSize)) *
            pass.output[static_cast<std::size_t>(o)] *
            (1.0 - pass.output[static_cast<std::size_t>(o)]);
    }
    loss /= static_cast<double>(kOutputSize);

    std::vector<std::vector<double>> gradW2(
        static_cast<std::size_t>(kOutputSize),
        std::vector<double>(static_cast<std::size_t>(hiddenSize_), 0.0)
    );
    std::vector<double> gradB2(static_cast<std::size_t>(kOutputSize), 0.0);
    std::vector<double> hiddenGrad(static_cast<std::size_t>(hiddenSize_), 0.0);

    for (int o = 0; o < kOutputSize; ++o) {
        gradB2[static_cast<std::size_t>(o)] = dOutputRaw[static_cast<std::size_t>(o)];
        for (int h = 0; h < hiddenSize_; ++h) {
            gradW2[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] =
                dOutputRaw[static_cast<std::size_t>(o)] * pass.hidden[static_cast<std::size_t>(h)];
            hiddenGrad[static_cast<std::size_t>(h)] +=
                w2_[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] *
                dOutputRaw[static_cast<std::size_t>(o)];
        }
    }

    std::vector<std::vector<double>> gradW1(
        static_cast<std::size_t>(hiddenSize_),
        std::vector<double>(input.size(), 0.0)
    );
    std::vector<double> gradB1(static_cast<std::size_t>(hiddenSize_), 0.0);
    for (int h = 0; h < hiddenSize_; ++h) {
        const double hiddenValue = pass.hidden[static_cast<std::size_t>(h)];
        const double dHiddenRaw = hiddenGrad[static_cast<std::size_t>(h)] * (1.0 - hiddenValue * hiddenValue);
        gradB1[static_cast<std::size_t>(h)] = dHiddenRaw;
        for (std::size_t i = 0; i < input.size(); ++i) {
            gradW1[static_cast<std::size_t>(h)][i] = dHiddenRaw * input[i];
        }
    }

    const double lr = options_.surrogateLearningRate;
    for (int o = 0; o < kOutputSize; ++o) {
        b2_[static_cast<std::size_t>(o)] -= lr * gradB2[static_cast<std::size_t>(o)];
        for (int h = 0; h < hiddenSize_; ++h) {
            w2_[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] -=
                lr * gradW2[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)];
        }
    }
    for (int h = 0; h < hiddenSize_; ++h) {
        b1_[static_cast<std::size_t>(h)] -= lr * gradB1[static_cast<std::size_t>(h)];
        for (std::size_t i = 0; i < input.size(); ++i) {
            w1_[static_cast<std::size_t>(h)][i] -= lr * gradW1[static_cast<std::size_t>(h)][i];
        }
    }

    return loss;
}

std::vector<double> GradientCalibrator::gradientTowardTarget(const std::vector<double>& input) const {
    const ForwardPass pass = forward(input);
    std::vector<double> dOutputRaw(static_cast<std::size_t>(kOutputSize), 0.0);
    for (int o = 0; o < kOutputSize; ++o) {
        const double error =
            pass.output[static_cast<std::size_t>(o)] - targetNormalized_[static_cast<std::size_t>(o)];
        dOutputRaw[static_cast<std::size_t>(o)] =
            (2.0 * error / static_cast<double>(kOutputSize)) *
            pass.output[static_cast<std::size_t>(o)] *
            (1.0 - pass.output[static_cast<std::size_t>(o)]);
    }

    std::vector<double> hiddenGrad(static_cast<std::size_t>(hiddenSize_), 0.0);
    for (int o = 0; o < kOutputSize; ++o) {
        for (int h = 0; h < hiddenSize_; ++h) {
            hiddenGrad[static_cast<std::size_t>(h)] +=
                w2_[static_cast<std::size_t>(o)][static_cast<std::size_t>(h)] *
                dOutputRaw[static_cast<std::size_t>(o)];
        }
    }

    std::vector<double> inputGrad(input.size(), 0.0);
    for (int h = 0; h < hiddenSize_; ++h) {
        const double hiddenValue = pass.hidden[static_cast<std::size_t>(h)];
        const double dHiddenRaw = hiddenGrad[static_cast<std::size_t>(h)] * (1.0 - hiddenValue * hiddenValue);
        for (std::size_t i = 0; i < input.size(); ++i) {
            inputGrad[i] += w1_[static_cast<std::size_t>(h)][i] * dHiddenRaw;
        }
    }

    inputGrad.resize(parameters_.size());
    return inputGrad;
}

SimulationConfig GradientCalibrator::propose(unsigned long long attempt) {
    const double decay = 1.0 / std::sqrt(1.0 + static_cast<double>(cycle_) / 5000.0);
    const double exploration = std::max(0.015, options_.explorationScale * decay);

    lastCandidate_ = parameters_;
    for (std::size_t i = 0; i < lastCandidate_.size(); ++i) {
        if (specs_[i].locked) {
            continue;
        }
        const std::uint64_t material =
            baseSeed_ ^
            (static_cast<std::uint64_t>(contest_) * 0xd6e8feb86659fd93ULL) ^
            (attempt * 0xa0761d6478bd642fULL) ^
            (static_cast<std::uint64_t>(i + 1) * 0xe7037ed1a0b428dbULL) ^
            (static_cast<std::uint64_t>(options_.workerIndex + 1) * 0x8ebc6af09c88c6e3ULL);
        lastCandidate_[i] = clamp01(lastCandidate_[i] + exploration * signedNoise(material));
    }

    lastInput_ = buildInput(lastCandidate_);

    SimulationConfig config = baseConfig_;
    applyCandidateToConfig(lastCandidate_, config);

    std::uint64_t seedMaterial =
        baseSeed_ ^
        (static_cast<std::uint64_t>(contest_) * 0xd6e8feb86659fd93ULL) ^
        (attempt * 0xa0761d6478bd642fULL) ^
        (static_cast<std::uint64_t>(options_.workerIndex + 1) * 0xe7037ed1a0b428dbULL);
    for (double value : lastCandidate_) {
        seedMaterial ^= static_cast<std::uint64_t>(std::llround(value * 1000000.0)) + 0x9e3779b97f4a7c15ULL;
        seedMaterial = splitMix64(seedMaterial);
    }
    config.seed = splitMix64(seedMaterial);
    config.writeArtifacts = false;
    return config;
}

GradientCalibrationTelemetry GradientCalibrator::observe(const std::vector<int>& sortedResult, bool complete) {
    if (lastInput_.empty()) {
        throw std::runtime_error("gradient calibrator observe called before propose");
    }

    const std::vector<double> observed = normalizeResult(sortedResult, complete);
    const double surrogateLoss = trainSurrogate(lastInput_, observed);
    const std::vector<double> gradient = gradientTowardTarget(lastInput_);

    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        if (specs_[i].locked) {
            continue;
        }
        parameters_[i] = clamp01(parameters_[i] - options_.parameterLearningRate * gradient[i]);
    }

    ++cycle_;

    GradientCalibrationTelemetry telemetry;
    telemetry.loss = resultLoss(observed, complete);
    telemetry.surrogateLoss = surrogateLoss;
    telemetry.learningRate = options_.parameterLearningRate;
    telemetry.surrogateLearningRate = options_.surrogateLearningRate;
    telemetry.explorationScale = options_.explorationScale;
    telemetry.matchedNumbers = countMatches(sortedResult);
    telemetry.complete = complete;
    telemetry.cycle = cycle_;
    telemetry.errorVector.reserve(targetNormalized_.size());
    for (std::size_t i = 0; i < targetNormalized_.size(); ++i) {
        telemetry.errorVector.push_back(observed[i] - targetNormalized_[i]);
    }
    telemetry.gradient = gradient;
    telemetry.parameterValues = denormalizedParameters();
    telemetry.parameterNames.reserve(specs_.size());
    for (const ParameterSpec& spec : specs_) {
        telemetry.parameterNames.push_back(spec.name);
    }
    return telemetry;
}

std::vector<double> GradientCalibrator::normalizeResult(const std::vector<int>& sortedResult, bool complete) const {
    std::vector<double> observed(static_cast<std::size_t>(kOutputSize), 1.0);
    std::vector<int> sorted = sortedResult;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < observed.size() && i < sorted.size(); ++i) {
        observed[i] = normalizeNumber(sorted[i]);
    }
    if (!complete) {
        for (std::size_t i = sorted.size(); i < observed.size(); ++i) {
            observed[i] = 1.0;
        }
    }
    return observed;
}

double GradientCalibrator::resultLoss(const std::vector<double>& observed, bool complete) const {
    double loss = 0.0;
    for (std::size_t i = 0; i < targetNormalized_.size(); ++i) {
        const double error = observed[i] - targetNormalized_[i];
        loss += error * error;
    }
    loss /= static_cast<double>(targetNormalized_.size());
    if (!complete) {
        loss += 0.25;
    }
    return loss;
}

int GradientCalibrator::countMatches(const std::vector<int>& sortedResult) const {
    std::vector<int> sorted = sortedResult;
    std::sort(sorted.begin(), sorted.end());
    int matches = 0;
    std::size_t left = 0;
    std::size_t right = 0;
    while (left < sorted.size() && right < targetNumbers_.size()) {
        if (sorted[left] == targetNumbers_[right]) {
            ++matches;
            ++left;
            ++right;
        } else if (sorted[left] < targetNumbers_[right]) {
            ++left;
        } else {
            ++right;
        }
    }
    return matches;
}

void GradientCalibrator::applyCandidateToConfig(const std::vector<double>& candidate, SimulationConfig& config) const {
    config.upwardJetForce = denormalizeParameter(candidate[0], specs_[0].minValue, specs_[0].maxValue);
    config.turbulenceForce = denormalizeParameter(candidate[1], specs_[1].minValue, specs_[1].maxValue);
    config.restitution = denormalizeParameter(candidate[2], specs_[2].minValue, specs_[2].maxValue);
    config.friction = denormalizeParameter(candidate[3], specs_[3].minValue, specs_[3].maxValue);
    config.captureRadius = denormalizeParameter(candidate[4], specs_[4].minValue, specs_[4].maxValue);
    config.captureMaxSpeed = denormalizeParameter(candidate[5], specs_[5].minValue, specs_[5].maxValue);
    config.normalDamping = denormalizeParameter(candidate[6], specs_[6].minValue, specs_[6].maxValue);
    config.tangentialDamping = denormalizeParameter(candidate[7], specs_[7].minValue, specs_[7].maxValue);
    if (!options_.fixedMinMixTime) {
        config.minMixTime = denormalizeParameter(candidate[8], specs_[8].minValue, specs_[8].maxValue);
    }
    if (!options_.fixedCaptureMode) {
        config.captureMode = candidate[9] >= 0.5 ? "top-side" : "outlet";
    }
    config.captureRequiresDownwardVelocity = config.captureMode == "outlet";
}

std::vector<double> GradientCalibrator::denormalizedParameters() const {
    std::vector<double> values;
    values.reserve(parameters_.size());
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        values.push_back(denormalizeParameter(parameters_[i], specs_[i].minValue, specs_[i].maxValue));
    }
    return values;
}
