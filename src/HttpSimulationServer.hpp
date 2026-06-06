#pragma once

#include <cstdint>

#include "SimulationConfig.hpp"

class HttpSimulationServer {
public:
    HttpSimulationServer(SimulationConfig baseConfig, int port);

    int run();

private:
    SimulationConfig baseConfig_;
    int port_{8080};
};
