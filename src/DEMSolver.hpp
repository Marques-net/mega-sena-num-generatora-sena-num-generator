#pragma once

#include <vector>

#include "Ball.hpp"
#include "RandomField.hpp"
#include "SimulationConfig.hpp"

class DEMSolver {
public:
    explicit DEMSolver(const SimulationConfig& config);

    void step(std::vector<Ball>& balls, double time);

private:
    SimulationConfig config_;
    RandomField randomField_;

    void applyExternalForces(const Ball& ball, double time, std::vector<Vec3>& forces, std::size_t index);
    void applyBallBallContacts(std::vector<Ball>& balls, std::vector<Vec3>& forces);
    void integrate(std::vector<Ball>& balls, const std::vector<Vec3>& forces);
    void applyWallContacts(Ball& ball);
    void applyWallResponse(Ball& ball, const Vec3& outwardNormal);
};

