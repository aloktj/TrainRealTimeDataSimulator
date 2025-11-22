#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#if __has_include(<trdp_if_light.h>)
#include <trdp_if_light.h>
#elif __has_include(<trdp_if.h>)
#include <trdp_if.h>
#elif __has_include(<tau_api.h>)
#include <tau_api.h>
#else
#error "TRDP headers not found during compilation. Ensure TRDP include path is configured."
#endif

#include "config_manager.hpp"
#include "data_types.hpp"

namespace engine {
namespace pd {
class PdEngine;
struct PdTelegramRuntime;
}
namespace md {
class MdEngine;
struct MdSessionRuntime;
}
} // namespace engine

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

    // TRDP session
    TRDP_APP_SESSION_T trdpSession { nullptr };

    // Engine back-references for callbacks
    engine::pd::PdEngine* pdEngine { nullptr };
    engine::md::MdEngine* mdEngine { nullptr };

    // Threads (optional)
    std::thread pdPublisherThread;
    std::thread diagThread;

    // Global running flag
    bool running { false };
};

} // namespace trdp_sim
