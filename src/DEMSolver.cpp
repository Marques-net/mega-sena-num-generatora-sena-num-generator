#include "DEMSolver.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;

double clamp(double value, double minValue, double maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}
}

DEMSolver::DEMSolver(const SimulationConfig& config)
    : config_(config), randomField_(config.seed ^ 0x9e3779b97f4a7c15ULL, config.ballCount + 1) {}

void DEMSolver::step(std::vector<Ball>& balls, double time) {
    randomField_.update(config_.timeStep, config_);

    std::vector<Vec3> forces(balls.size());
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (balls[i].active()) {
            applyExternalForces(balls[i], time, forces, i);
        }
    }

    applyBallBallContacts(balls, forces);
    integrate(balls, forces);

    for (Ball& ball : balls) {
        if (ball.active()) {
            applyWallContacts(ball);
        }
    }
}

void DEMSolver::applyExternalForces(
    const Ball& ball,
    double time,
    std::vector<Vec3>& forces,
    std::size_t index
) {
    forces[index] += Vec3{0.0, -ball.mass * config_.gravity, 0.0};

    const double area = pi * ball.radius * ball.radius;
    const double speed = ball.velocity.length();
    if (speed > 1e-12) {
        const Vec3 drag = ball.velocity * (-0.5 * config_.airDensity * config_.dragCoefficient * area * speed);
        forces[index] += drag;
    }

    const double normalizedHeight = clamp(ball.position.y / config_.chamberHeight, 0.0, 1.0);
    const double baseProfile = 0.20 + std::exp(-normalizedHeight / 0.35);
    const double centerX = config_.chamberWidth * 0.5;
    const double centerZ = config_.chamberDepth * 0.5;
    const double dx = ball.position.x - centerX;
    const double dz = ball.position.z - centerZ;
    const double jetRadius = std::max(config_.ballRadius, config_.chamberWidth * 0.20);
    const double radialShape = 0.35 + 0.65 * std::exp(-(dx * dx + dz * dz) / (2.0 * jetRadius * jetRadius));
    const double noise = randomField_.jetNoise(config_.jetNoiseAmplitude);
    const double swirl = 0.18 * std::sin(7.0 * time + static_cast<double>(ball.id));
    const Vec3 jetDirection = Vec3{-0.35 * dx / config_.chamberWidth, 1.0, swirl}.normalized();
    forces[index] += jetDirection * (config_.upwardJetForce * baseProfile * radialShape * noise);

    forces[index] += randomField_.turbulence(ball.id) * config_.turbulenceForce;
}

void DEMSolver::applyBallBallContacts(std::vector<Ball>& balls, std::vector<Vec3>& forces) {
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (!balls[i].active()) {
            continue;
        }
        for (std::size_t j = i + 1; j < balls.size(); ++j) {
            if (!balls[j].active()) {
                continue;
            }

            Vec3 delta = balls[j].position - balls[i].position;
            double distance = delta.length();
            const double minDistance = balls[i].radius + balls[j].radius;
            if (distance >= minDistance) {
                continue;
            }

            Vec3 normal{1.0, 0.0, 0.0};
            if (distance > 1e-12) {
                normal = delta / distance;
            } else {
                distance = minDistance;
            }

            const double penetration = minDistance - distance;
            const Vec3 relativeVelocity = balls[j].velocity - balls[i].velocity;
            const double relativeNormalVelocity = Vec3::dot(relativeVelocity, normal);
            double normalForceMagnitude =
                config_.normalStiffness * penetration - config_.normalDamping * relativeNormalVelocity;
            normalForceMagnitude = std::max(0.0, normalForceMagnitude);

            const Vec3 normalForce = normal * normalForceMagnitude;
            forces[i] -= normalForce;
            forces[j] += normalForce;

            const Vec3 tangentialVelocity = relativeVelocity - normal * relativeNormalVelocity;
            const double tangentialSpeed = tangentialVelocity.length();
            if (tangentialSpeed > 1e-9) {
                const double maxFriction = config_.friction * normalForceMagnitude;
                const double frictionMagnitude = std::min(maxFriction, config_.tangentialDamping * tangentialSpeed);
                const Vec3 frictionForce = tangentialVelocity.normalized() * frictionMagnitude;
                forces[i] += frictionForce;
                forces[j] -= frictionForce;
            }

            const Vec3 correction = normal * (0.5 * penetration + 1e-7);
            balls[i].position -= correction;
            balls[j].position += correction;
        }
    }
}

void DEMSolver::integrate(std::vector<Ball>& balls, const std::vector<Vec3>& forces) {
    for (std::size_t i = 0; i < balls.size(); ++i) {
        Ball& ball = balls[i];
        if (!ball.active()) {
            ball.acceleration = {};
            continue;
        }
        ball.acceleration = forces[i] / ball.mass;
        ball.velocity += ball.acceleration * config_.timeStep;
        ball.position += ball.velocity * config_.timeStep;
    }
}

void DEMSolver::applyWallContacts(Ball& ball) {
    if (config_.cylindricalChamber) {
        const double centerX = config_.chamberWidth * 0.5;
        const double centerY = config_.chamberHeight * 0.5;
        const double radius = std::min(config_.chamberWidth, config_.chamberHeight) * 0.5;
        const double allowedRadius = radius - ball.radius;

        const double dx = ball.position.x - centerX;
        const double dy = ball.position.y - centerY;
        const double radialDistance = std::sqrt(dx * dx + dy * dy);
        if (radialDistance > allowedRadius) {
            const Vec3 normal = radialDistance > 1e-12 ? Vec3{dx / radialDistance, dy / radialDistance, 0.0}
                                                       : Vec3{0.0, -1.0, 0.0};
            ball.position.x = centerX + normal.x * allowedRadius;
            ball.position.y = centerY + normal.y * allowedRadius;
            applyWallResponse(ball, normal);
        }
    } else {
        if (ball.position.x < ball.radius) {
            ball.position.x = ball.radius;
            applyWallResponse(ball, {-1.0, 0.0, 0.0});
        }
        if (ball.position.x > config_.chamberWidth - ball.radius) {
            ball.position.x = config_.chamberWidth - ball.radius;
            applyWallResponse(ball, {1.0, 0.0, 0.0});
        }
        if (ball.position.y < ball.radius) {
            ball.position.y = ball.radius;
            applyWallResponse(ball, {0.0, -1.0, 0.0});
        }
        if (ball.position.y > config_.chamberHeight - ball.radius) {
            ball.position.y = config_.chamberHeight - ball.radius;
            applyWallResponse(ball, {0.0, 1.0, 0.0});
        }
    }

    if (ball.position.z < ball.radius) {
        ball.position.z = ball.radius;
        applyWallResponse(ball, {0.0, 0.0, -1.0});
    }
    if (ball.position.z > config_.chamberDepth - ball.radius) {
        ball.position.z = config_.chamberDepth - ball.radius;
        applyWallResponse(ball, {0.0, 0.0, 1.0});
    }
}

void DEMSolver::applyWallResponse(Ball& ball, const Vec3& outwardNormal) {
    const double normalVelocity = Vec3::dot(ball.velocity, outwardNormal);
    if (normalVelocity <= 0.0) {
        return;
    }

    const Vec3 velocityNormal = outwardNormal * normalVelocity;
    const Vec3 velocityTangent = ball.velocity - velocityNormal;
    ball.velocity = velocityTangent * std::max(0.0, 1.0 - ball.friction)
                  - velocityNormal * ball.restitution;
}

