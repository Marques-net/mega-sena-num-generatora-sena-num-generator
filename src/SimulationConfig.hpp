#pragma once

#include <cstdint>
#include <string>

struct SimulationConfig {
    int ballCount{60};
    int ballsToExtract{6};

    double timeStep{0.0005};
    double maxDuration{120.0};
    double minMixTime{10.0};
    double csvInterval{0.02};

    double chamberWidth{0.50};
    double chamberHeight{0.60};
    double chamberDepth{0.25};
    bool cylindricalChamber{true};

    double ballRadius{0.025};
    double ballMass{0.06};
    double restitution{0.85};
    double friction{0.2};

    double gravity{9.81};
    double airDensity{1.225};
    double dragCoefficient{0.47};

    double upwardJetForce{1.05};
    double jetNoiseAmplitude{0.25};
    double turbulenceForce{0.28};
    double turbulenceCorrelationTime{0.08};

    double normalStiffness{20000.0};
    double normalDamping{2.5};
    double tangentialDamping{0.35};

    std::string captureMode{"outlet"};
    double captureRadius{0.055};
    double captureMaxSpeed{4.0};
    bool captureRequiresDownwardVelocity{false};

    std::uint64_t seed{5489};
    std::string outputDir{"output"};
    bool writeArtifacts{true};
};
