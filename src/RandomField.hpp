#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "SimulationConfig.hpp"
#include "Vec3.hpp"

class RandomField {
public:
    explicit RandomField(std::uint64_t seed, int maxBallId)
        : rng_(seed), normal_(0.0, 1.0), uniform_(0.0, 1.0), field_(maxBallId + 1) {}

    void update(double dt, const SimulationConfig& config) {
        const double tau = std::max(config.turbulenceCorrelationTime, dt);
        const double decay = std::exp(-dt / tau);
        const double innovationScale = std::sqrt(std::max(0.0, 1.0 - decay * decay));

        for (auto& value : field_) {
            const Vec3 randomVector{normal_(rng_), normal_(rng_), normal_(rng_)};
            value = value * decay + randomVector * innovationScale;
        }
    }

    [[nodiscard]] Vec3 turbulence(int ballId) const {
        if (ballId < 0 || static_cast<std::size_t>(ballId) >= field_.size()) {
            return {};
        }
        return field_[static_cast<std::size_t>(ballId)];
    }

    [[nodiscard]] double jetNoise(double amplitude) {
        const double centered = 2.0 * uniform_(rng_) - 1.0;
        return std::max(0.0, 1.0 + amplitude * centered);
    }

private:
    std::mt19937_64 rng_;
    std::normal_distribution<double> normal_;
    std::uniform_real_distribution<double> uniform_;
    std::vector<Vec3> field_;
};

