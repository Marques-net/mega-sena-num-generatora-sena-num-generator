#pragma once

#include <filesystem>
#include <vector>

#include "Ball.hpp"
#include "CaptureZone.hpp"
#include "CsvWriter.hpp"
#include "DEMSolver.hpp"
#include "SimulationConfig.hpp"

class LotteryMachine {
public:
    explicit LotteryMachine(SimulationConfig config);

    void initialize();
    void run();

    [[nodiscard]] const std::vector<int>& extractedBalls() const {
        return extracted_;
    }

    [[nodiscard]] double finalTime() const {
        return currentTime_;
    }

private:
    SimulationConfig config_;
    DEMSolver solver_;
    CaptureZone captureZone_;
    std::vector<Ball> balls_;
    std::vector<int> extracted_;
    double currentTime_{0.0};

    [[nodiscard]] Vec3 randomInitialPosition(std::mt19937_64& rng) const;
    [[nodiscard]] bool overlapsExisting(const Vec3& position) const;
    [[nodiscard]] static std::string colorForId(int id);

    void tryCapture();
    void writeResultFile() const;
};

