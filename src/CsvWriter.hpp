#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include "Ball.hpp"

class CsvWriter {
public:
    explicit CsvWriter(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        out_.open(path);
        if (!out_) {
            throw std::runtime_error("failed to open CSV file: " + path.string());
        }
        out_ << "time,id,x,y,z,vx,vy,vz,status\n";
        out_ << std::fixed << std::setprecision(8);
    }

    void writeSnapshot(double time, const std::vector<Ball>& balls) {
        for (const Ball& ball : balls) {
            out_ << time << ','
                 << ball.id << ','
                 << ball.position.x << ','
                 << ball.position.y << ','
                 << ball.position.z << ','
                 << ball.velocity.x << ','
                 << ball.velocity.y << ','
                 << ball.velocity.z << ','
                 << statusToString(ball.status) << '\n';
        }
    }

private:
    std::ofstream out_;
};

