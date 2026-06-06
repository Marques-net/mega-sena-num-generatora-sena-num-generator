#pragma once

#include <string>

#include "Vec3.hpp"

enum class BallStatus {
    Active,
    Extracted,
};

inline const char* statusToString(BallStatus status) {
    return status == BallStatus::Active ? "active" : "extracted";
}

struct Ball {
    int id{0};
    Vec3 position{};
    Vec3 velocity{};
    Vec3 acceleration{};
    double radius{0.025};
    double mass{0.06};
    double restitution{0.85};
    double friction{0.2};
    std::string color{"white"};
    BallStatus status{BallStatus::Active};

    [[nodiscard]] bool active() const {
        return status == BallStatus::Active;
    }
};

