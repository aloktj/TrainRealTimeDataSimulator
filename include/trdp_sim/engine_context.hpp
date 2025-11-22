#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "config_manager.hpp"
#include "data_types.hpp"

namespace trdp_sim {

namespace pd = engine::pd;
namespace md = engine::md;
namespace cfg = config;
namespace diag_ns = diag;

struct EngineContext
{
    cfg::DeviceConfig deviceConfig;

    // Dataset definitions & instances
    std::unordered_map<uint32_t, data::DataSetDef> dataSetDefs;
    std::unordered_map<uint32_t, std::unique_ptr<data::DataSetInstance>> dataSetInstances;

    // PD telegrams
    std::vector<std::unique_ptr<pd::PdTelegramRuntime>> pdTelegrams;

    // MD sessions (sessionId → runtime)
    std::unordered_map<uint32_t, std::unique_ptr<md::MdSessionRuntime>> mdSessions;

    // TRDP session (opaque pointer or actual TCNOpen type)
    void* trdpSession { nullptr }; // TODO: replace with TRDP_APP_SESSION_T

    // Threads (optional)
    std::thread pdPublisherThread;
    std::thread diagThread;

    // Global running flag
    bool running { false };
};

} // namespace trdp_sim
