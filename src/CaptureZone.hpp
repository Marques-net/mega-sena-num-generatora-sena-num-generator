#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Ball.hpp"
#include "SimulationConfig.hpp"
#include "Vec3.hpp"

class CaptureZone {
public:
    CaptureZone() = default;

    explicit CaptureZone(const SimulationConfig& config)
        : radius_(config.captureRadius),
          maxSpeed_(config.captureMaxSpeed),
          requiresDownwardVelocity_(config.captureRequiresDownwardVelocity) {
        if (config.captureMode == "top-side") {
            center_ = {
                config.chamberWidth * 0.84,
                config.chamberHeight * 0.82,
                config.chamberDepth * 0.50,
            };
        } else {
            // Default adjusted from the video: central lower outlet/gate feeding the ramp.
            center_ = {
                config.chamberWidth * 0.50,
                config.chamberHeight * 0.16,
                config.chamberDepth * 0.50,
            };
        }
    }

    [[nodiscard]] bool contains(const Ball& ball) const {
        if (!ball.active()) {
            return false;
        }
        const double distance = (ball.position - center_).length();
        if (distance > radius_) {
            return false;
        }
        if (ball.velocity.length() > maxSpeed_) {
            return false;
        }
        if (requiresDownwardVelocity_ && ball.velocity.y > 0.0) {
            return false;
        }
        return true;
    }

    [[nodiscard]] double score(const Ball& ball) const {
        const double distance = (ball.position - center_).length();
        return distance + 0.02 * ball.velocity.length();
    }

    [[nodiscard]] const Vec3& center() const {
        return center_;
    }

private:
    Vec3 center_{};
    double radius_{0.055};
    double maxSpeed_{4.0};
    bool requiresDownwardVelocity_{false};
};

