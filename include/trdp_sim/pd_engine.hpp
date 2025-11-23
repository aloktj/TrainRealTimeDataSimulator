#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "config_manager.hpp"
#include "data_types.hpp"
#include "engine_context.hpp"

namespace trdp_sim::trdp
{
    class TrdpAdapter;
}

namespace engine::pd
{

    enum class Direction
    {
        PUBLISH,
        SUBSCRIBE
    };

    struct PdRuntimeStats
    {
        uint64_t                              txCount{0};
        uint64_t                              rxCount{0};
        uint64_t                              timeoutCount{0};
        uint64_t                              lastSeqNumber{0};
        uint64_t                              stressBursts{0};
        uint64_t                              busFailureDrops{0};
        uint64_t                              redundancySwitches{0};
        std::chrono::steady_clock::time_point lastTxTime{};
        std::chrono::steady_clock::time_point lastRxTime{};
        double                                lastCycleJitterUs{0.0};
        double                                lastInterarrivalUs{0.0};
        bool                                  timedOut{false};
        std::chrono::system_clock::time_point lastTxWall{};
        std::chrono::system_clock::time_point lastRxWall{};
    };

    struct PdTelegramRuntime
    {
        struct PublicationChannel
        {
            TRDP_PUB_T    handle{nullptr};
            TRDP_IP_ADDR_T destIp{0};
        };

        const config::TelegramConfig*     cfg{nullptr};
        const config::BusInterfaceConfig* ifaceCfg{nullptr};
        const config::PdComParameter*     pdComCfg{nullptr};
        Direction                         direction{Direction::PUBLISH};
        data::DataSetInstance*            dataset{nullptr};
        PdRuntimeStats                    stats;
        bool                              enabled{false};
        bool                              redundantActive{false};
        uint32_t                          activeChannel{0};
        std::vector<PublicationChannel>   pubChannels;
        TRDP_SUB_T                        subHandle{nullptr};
        bool                              sendNow{false};
        std::mutex                        mtx;
    };

    class PdEngine
    {
      public:
        PdEngine(trdp_sim::EngineContext& ctx, trdp_sim::trdp::TrdpAdapter& adapter);
        ~PdEngine();

        void initializeFromConfig();
        void start();
        void stop();

        void enableTelegram(uint32_t comId, bool enable);
        void triggerSendNow(uint32_t comId);

        // Dataset access:
        data::DataSetInstance* getDataSetInstance(uint32_t dataSetId);

        // Called from TRDP adapter:
        void onPdReceived(uint32_t comId, const uint8_t* data, std::size_t len);

        // Exposed for deterministic scheduling tests and single-tick processing
        void processPublishersOnce(std::chrono::steady_clock::time_point now);

        bool isRunning() const
        {
            return m_running.load();
        }

      private:
        void runPublisherLoop();

        trdp_sim::EngineContext&     m_ctx;
        trdp_sim::trdp::TrdpAdapter& m_adapter;
        std::atomic<bool>            m_running{false};
        std::thread                  m_thread;
    };

} // namespace engine::pd
