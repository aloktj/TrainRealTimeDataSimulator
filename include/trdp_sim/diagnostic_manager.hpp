#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace trdp_sim
{
    struct EngineContext;
    namespace trdp
    {
        class TrdpAdapter;
    }
} // namespace trdp_sim

namespace engine
{
    namespace pd
    {
        class PdEngine;
    }
    namespace md
    {
        class MdEngine;
    }
} // namespace engine

namespace diag
{

    enum class Severity
    {
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

    struct Event
    {
        std::chrono::system_clock::time_point timestamp;
        Severity                              severity;
        std::string                           component;
        std::string                           message;
        std::optional<std::string>            extraJson;
    };

    struct ThreadStatus
    {
        bool pdThreadRunning{false};
        bool mdThreadRunning{false};
        bool diagThreadRunning{false};
        bool trdpThreadRunning{false};
    };

    struct PdMetrics
    {
        std::size_t telegrams{0};
        uint64_t    txCount{0};
        uint64_t    rxCount{0};
        uint64_t    timeoutCount{0};
        double      maxCycleJitterUs{0.0};
        double      maxInterarrivalUs{0.0};
        uint64_t    activeTimeouts{0};
        std::chrono::system_clock::time_point latestRxWall{};
        std::chrono::system_clock::time_point latestTxWall{};
    };

    struct MdMetrics
    {
        std::size_t sessions{0};
        uint64_t    txCount{0};
        uint64_t    rxCount{0};
        uint64_t    retryCount{0};
        uint64_t    timeoutCount{0};
        double      maxLatencyUs{0.0};
    };

    struct TrdpMetrics
    {
        uint64_t                initErrors{0};
        uint64_t                publishErrors{0};
        uint64_t                subscribeErrors{0};
        uint64_t                pdSendErrors{0};
        uint64_t                mdRequestErrors{0};
        uint64_t                mdReplyErrors{0};
        uint64_t                eventLoopErrors{0};
        std::optional<uint32_t> lastErrorCode;
    };

    struct MetricsSnapshot
    {
        std::chrono::system_clock::time_point timestamp;
        ThreadStatus                          threads;
        PdMetrics                             pd;
        MdMetrics                             md;
        TrdpMetrics                           trdp;
    };

    struct LogConfig
    {
        Severity                   minimumSeverity{Severity::INFO};
        bool                       logToStdout{true};
        std::optional<std::string> filePath{};
        std::size_t                maxFileSizeBytes{0};
    };

    struct PcapConfig
    {
        bool                       enabled{false};
        bool                       captureTx{true};
        bool                       captureRx{true};
        std::optional<std::string> filePath{};
        std::size_t                maxFileSizeBytes{0};
        std::size_t                maxFiles{2};
    };

    class DiagnosticManager
    {
      public:
        DiagnosticManager(trdp_sim::EngineContext& ctx, engine::pd::PdEngine& pd, engine::md::MdEngine& md,
                          trdp_sim::trdp::TrdpAdapter& adapter, const LogConfig& cfg = {},
                          const PcapConfig& pcapCfg = {});
        ~DiagnosticManager();

        void start();
        void stop();

        void log(Severity sev, const std::string& component, const std::string& message,
                 const std::optional<std::string>& extraJson = std::nullopt);

        std::vector<Event> fetchRecent(std::size_t maxEvents);
        MetricsSnapshot    getMetrics() const;
        void               updateLogConfig(const LogConfig& cfg);

        void enablePcapCapture(bool enable);
        void updatePcapConfig(const PcapConfig& cfg);
        void writePacketToPcap(const uint8_t* data, std::size_t len, bool isTx);

      private:
        void        workerThreadFn();
        void        rotateLogIfNeeded();
        void        persistEvent(const Event& ev);
        void        pollMetrics();
        bool        shouldLog(Severity sev) const;
        std::string severityToString(Severity sev) const;
        std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) const;
        bool        ensurePcapFileUnlocked(std::size_t nextPacketSize);
        void        rotatePcapFilesUnlocked();
        void        writePcapGlobalHeader();

        trdp_sim::EngineContext&     m_ctx;
        engine::pd::PdEngine&        m_pd;
        engine::md::MdEngine&        m_md;
        trdp_sim::trdp::TrdpAdapter& m_adapter;

        LogConfig             m_logCfg{};
        mutable std::mutex    m_logCfgMtx;
        std::filesystem::path m_logPath;
        std::ofstream         m_logFile;

        std::mutex        m_mtx;
        std::deque<Event> m_queue;
        std::atomic<bool> m_running{false};
        std::thread       m_thread;

        mutable std::mutex                    m_metricsMtx;
        MetricsSnapshot                       m_metrics{};
        std::chrono::steady_clock::time_point m_lastPoll{};
        std::chrono::milliseconds             m_pollInterval{std::chrono::milliseconds(1000)};

        PcapConfig            m_pcapCfg{};
        std::filesystem::path m_pcapPath;
        std::ofstream         m_pcapFile;
        std::mutex            m_pcapMtx;
        std::size_t           m_pcapBytesWritten{0};
    };

} // namespace diag
