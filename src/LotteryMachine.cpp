#include "LotteryMachine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>

LotteryMachine::LotteryMachine(SimulationConfig config)
    : config_(std::move(config)), solver_(config_), captureZone_(config_) {}

void LotteryMachine::initialize() {
    balls_.clear();
    extracted_.clear();
    currentTime_ = 0.0;

    std::mt19937_64 rng(config_.seed);
    std::vector<int> ids(static_cast<std::size_t>(config_.ballCount));
    std::iota(ids.begin(), ids.end(), 1);
    std::shuffle(ids.begin(), ids.end(), rng);

    balls_.reserve(static_cast<std::size_t>(config_.ballCount));
    std::uniform_real_distribution<double> velocityDist(-0.08, 0.08);

    for (int i = 0; i < config_.ballCount; ++i) {
        Vec3 position{};
        bool placed = false;
        for (int attempt = 0; attempt < 20000; ++attempt) {
            position = randomInitialPosition(rng);
            if (!overlapsExisting(position)) {
                placed = true;
                break;
            }
        }
        if (!placed) {
            throw std::runtime_error("failed to place all balls without overlap");
        }

        const int id = ids[static_cast<std::size_t>(i)];
        Ball ball;
        ball.id = id;
        ball.position = position;
        ball.velocity = {velocityDist(rng), velocityDist(rng), velocityDist(rng)};
        ball.radius = config_.ballRadius;
        ball.mass = config_.ballMass;
        ball.restitution = config_.restitution;
        ball.friction = config_.friction;
        ball.color = colorForId(id);
        balls_.push_back(ball);
    }
}

void LotteryMachine::run() {
    if (balls_.empty()) {
        initialize();
    }

    std::unique_ptr<CsvWriter> csv;
    if (config_.writeArtifacts) {
        const std::filesystem::path outputDir(config_.outputDir);
        std::filesystem::create_directories(outputDir);
        csv = std::make_unique<CsvWriter>(outputDir / "trajectory.csv");
    }

    double nextCsvTime = 0.0;
    if (csv) {
        csv->writeSnapshot(currentTime_, balls_);
    }

    while (currentTime_ < config_.maxDuration &&
           static_cast<int>(extracted_.size()) < config_.ballsToExtract) {
        solver_.step(balls_, currentTime_);
        currentTime_ += config_.timeStep;
        tryCapture();

        if (csv && currentTime_ + 1e-12 >= nextCsvTime) {
            csv->writeSnapshot(currentTime_, balls_);
            nextCsvTime += config_.csvInterval;
        }
    }

    if (csv) {
        csv->writeSnapshot(currentTime_, balls_);
    }
    if (config_.writeArtifacts) {
        writeResultFile();
    }
}

Vec3 LotteryMachine::randomInitialPosition(std::mt19937_64& rng) const {
    std::uniform_real_distribution<double> ux(config_.ballRadius, config_.chamberWidth - config_.ballRadius);
    std::uniform_real_distribution<double> uy(config_.ballRadius, config_.chamberHeight - config_.ballRadius);
    std::uniform_real_distribution<double> uz(config_.ballRadius, config_.chamberDepth - config_.ballRadius);

    if (!config_.cylindricalChamber) {
        return {ux(rng), uy(rng), uz(rng)};
    }

    const double centerX = config_.chamberWidth * 0.5;
    const double centerY = config_.chamberHeight * 0.5;
    const double chamberRadius = std::min(config_.chamberWidth, config_.chamberHeight) * 0.5 - config_.ballRadius;

    for (;;) {
        const Vec3 candidate{ux(rng), uy(rng), uz(rng)};
        const double dx = candidate.x - centerX;
        const double dy = candidate.y - centerY;
        if (dx * dx + dy * dy <= chamberRadius * chamberRadius) {
            return candidate;
        }
    }
}

bool LotteryMachine::overlapsExisting(const Vec3& position) const {
    const double minDistance = 2.0 * config_.ballRadius;
    const double minDistanceSquared = minDistance * minDistance;
    for (const Ball& ball : balls_) {
        if ((position - ball.position).lengthSquared() < minDistanceSquared) {
            return true;
        }
    }
    return false;
}

std::string LotteryMachine::colorForId(int id) {
    if (id <= 10) {
        return "yellow";
    }
    if (id <= 20) {
        return "orange";
    }
    if (id <= 30) {
        return "green";
    }
    if (id <= 40) {
        return "blue";
    }
    if (id <= 50) {
        return "violet";
    }
    return "red";
}

void LotteryMachine::tryCapture() {
    if (currentTime_ < config_.minMixTime) {
        return;
    }
    if (static_cast<int>(extracted_.size()) >= config_.ballsToExtract) {
        return;
    }

    auto best = balls_.end();
    double bestScore = 0.0;
    for (auto it = balls_.begin(); it != balls_.end(); ++it) {
        if (!captureZone_.contains(*it)) {
            continue;
        }
        const double score = captureZone_.score(*it);
        if (best == balls_.end() || score < bestScore) {
            best = it;
            bestScore = score;
        }
    }

    if (best == balls_.end()) {
        return;
    }

    best->status = BallStatus::Extracted;
    best->velocity = {};
    best->acceleration = {};
    extracted_.push_back(best->id);
}

void LotteryMachine::writeResultFile() const {
    const std::filesystem::path resultPath = std::filesystem::path(config_.outputDir) / "result.txt";
    std::ofstream out(resultPath);
    if (!out) {
        throw std::runtime_error("failed to open result file: " + resultPath.string());
    }

    out << "Mega-Sena DEM simulation result\n";
    out << "seed=" << config_.seed << '\n';
    out << "final_time=" << std::fixed << std::setprecision(4) << currentTime_ << " s\n";
    out << "capture_mode=" << config_.captureMode << '\n';
    out << "extraction_order=";
    for (std::size_t i = 0; i < extracted_.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << std::setw(2) << std::setfill('0') << extracted_[i];
    }
    out << std::setfill(' ') << '\n';

    std::vector<int> sorted = extracted_;
    std::sort(sorted.begin(), sorted.end());
    out << "sorted=";
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << std::setw(2) << std::setfill('0') << sorted[i];
    }
    out << std::setfill(' ') << '\n';
    out << "complete=" << (static_cast<int>(extracted_.size()) == config_.ballsToExtract ? "yes" : "no") << '\n';
}
