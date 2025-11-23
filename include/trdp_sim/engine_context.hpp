#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(TRDP_USE_STUBS)
#include "trdp_stub.hpp"
#elif __has_include(<trdp_if_light.h>)
#include <trdp_if_light.h>
#elif __has_include(<trdp_if.h>)
#include <trdp_if.h>
#elif __has_include(<tau_api.h>)
#include <tau_api.h>
#else
#include "trdp_stub.hpp"
#endif

#include "config_manager.hpp"
#include "data_types.hpp"

namespace engine
{
    namespace pd
    {
        class PdEngine;
        struct PdTelegramRuntime;
    } // namespace pd
    namespace md
    {
        class MdEngine;
        struct MdSessionRuntime;
    } // namespace md
} // namespace engine

namespace diag
{
    class DiagnosticManager;
}

namespace trdp_sim::trdp
{
    class TrdpAdapter;
}

namespace trdp_sim
{

    namespace pd  = engine::pd;
    namespace md  = engine::md;
    namespace cfg = config;

    struct SimulationControls
    {
        struct InjectionRule
        {
            bool     corruptComId{false};
            bool     corruptDataSetId{false};
            int      seqDelta{0};
            uint32_t delayMs{0};
            double   lossRate{0.0};
        };

        struct StressMode
        {
            bool     enabled{false};
            uint32_t pdCycleOverrideUs{0};
            uint32_t pdBurstTelegrams{0};
            uint32_t mdBurst{0};
            uint32_t mdIntervalUs{0};

            static constexpr std::size_t kMaxBurstTelegrams = 1000;
            static constexpr uint32_t    kMinCycleUs       = 1000;
        };

        struct RedundancySimulation
        {
            bool     forceSwitch{false};
            bool     busFailure{false};
            uint32_t failedChannel{0};
        };

        struct TimeSyncOffsets
        {
            int64_t ntpOffsetUs{0};
            int64_t ptpOffsetUs{0};
        };

        struct VirtualInstance
        {
            std::string        name;
            std::string        configPath;
            cfg::DeviceConfig  config;
        };

        mutable std::mutex                                 mtx;
        std::unordered_map<uint32_t, InjectionRule>        pdRules;
        std::unordered_map<uint32_t, InjectionRule>        mdRules;
        std::unordered_map<uint32_t, InjectionRule>        dataSetRules;
        StressMode                                         stress;
        RedundancySimulation                               redundancy;
        TimeSyncOffsets                                    timeSync;
        std::unordered_map<std::string, VirtualInstance>   instances;
        std::string                                        activeInstance;
    };

    struct PdRuntimeDeleter
    {
        void operator()(pd::PdTelegramRuntime* ptr) const;
    };

    struct MdSessionDeleter
    {
        void operator()(md::MdSessionRuntime* ptr) const;
    };

    struct EngineContext
    {
        EngineContext()                                = default;
        EngineContext(const EngineContext&)            = delete;
        EngineContext& operator=(const EngineContext&) = delete;
        EngineContext(EngineContext&&)                 = default;
        EngineContext& operator=(EngineContext&&)      = default;

        cfg::DeviceConfig deviceConfig;

        // Dataset definitions & instances
        std::unordered_map<uint32_t, data::DataSetDef>                       dataSetDefs;
        std::unordered_map<uint32_t, std::unique_ptr<data::DataSetInstance>> dataSetInstances;

        // PD telegrams
        std::vector<std::unique_ptr<pd::PdTelegramRuntime, PdRuntimeDeleter>> pdTelegrams;

        // MD sessions (sessionId → runtime)
        std::unordered_map<uint32_t, std::unique_ptr<md::MdSessionRuntime, MdSessionDeleter>> mdSessions;

        struct MulticastGroupState
        {
            std::string                ifaceName;
            std::string                address;
            std::optional<std::string> nic;
            std::optional<std::string> hostIp;
            bool                       joined{false};
        };

        mutable std::mutex            multicastMtx;
        std::vector<MulticastGroupState> multicastGroups;

        // TRDP session
        TRDP_APP_SESSION_T trdpSession{nullptr};

        // Engine back-references for callbacks
        engine::pd::PdEngine* pdEngine{nullptr};
        engine::md::MdEngine* mdEngine{nullptr};

        // Threads (optional)
        std::thread pdPublisherThread;
        std::thread diagThread;

        // Global running flag
        bool running{false};

        // Path of the active XML configuration on disk
        std::string configPath;

        // Diagnostics
        diag::DiagnosticManager* diagManager{nullptr};

        // Adapter reference for API utilities
        trdp::TrdpAdapter* trdpAdapter{nullptr};

        // Simulation and injection controls
        SimulationControls simulation;

        ~EngineContext();
    };

} // namespace trdp_sim
