#include "diagnostic_manager.hpp"

#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace diag {

namespace {

Severity severityFromChar(char c)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
    case 'D': return Severity::DEBUG;
    case 'I': return Severity::INFO;
    case 'W': return Severity::WARN;
    case 'E': return Severity::ERROR;
    case 'F': return Severity::FATAL;
    default: return Severity::INFO;
    }
}

} // namespace

DiagnosticManager::DiagnosticManager(trdp_sim::EngineContext& ctx,
                                     engine::pd::PdEngine& pd,
                                     engine::md::MdEngine& md,
                                     trdp_sim::trdp::TrdpAdapter& adapter,
                                     const LogConfig& cfg)
    : m_ctx(ctx)
    , m_pd(pd)
    , m_md(md)
    , m_adapter(adapter)
    , m_logCfg(cfg)
{
    if (ctx.deviceConfig.debug) {
        auto dbg = *ctx.deviceConfig.debug;
        m_logCfg.minimumSeverity = severityFromChar(dbg.level);
        if (!dbg.fileName.empty()) {
            m_logCfg.filePath = dbg.fileName;
            m_logCfg.logToStdout = false;
            m_logCfg.maxFileSizeBytes = dbg.fileSize;
        }
    }

    if (m_logCfg.filePath)
        m_logPath = *m_logCfg.filePath;
}

DiagnosticManager::~DiagnosticManager()
{
    stop();
}

void DiagnosticManager::start()
{
    if (m_running.exchange(true))
        return;
    m_thread = std::thread(&DiagnosticManager::workerThreadFn, this);
}

void DiagnosticManager::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
}

void DiagnosticManager::log(Severity sev, const std::string& component,
                            const std::string& message,
                            const std::optional<std::string>& extraJson)
{
    if (!shouldLog(sev))
        return;

    Event ev;
    ev.timestamp = std::chrono::system_clock::now();
    ev.severity = sev;
    ev.component = component;
    ev.message = message;
    ev.extraJson = extraJson;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.push_back(std::move(ev));
    }
}

std::vector<Event> DiagnosticManager::fetchRecent(std::size_t maxEvents)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    std::vector<Event> out;
    maxEvents = std::min(maxEvents, m_queue.size());
    auto it = m_queue.end();
    for (std::size_t i = 0; i < maxEvents; ++i) {
        --it;
        out.push_back(*it);
    }
    return out;
}

MetricsSnapshot DiagnosticManager::getMetrics() const
{
    std::lock_guard<std::mutex> lk(m_metricsMtx);
    return m_metrics;
}

void DiagnosticManager::updateLogConfig(const LogConfig& cfg)
{
    std::lock_guard<std::mutex> lk(m_logCfgMtx);
    m_logCfg = cfg;
    if (m_logCfg.filePath)
        m_logPath = *m_logCfg.filePath;
}

void DiagnosticManager::enablePcapCapture(bool enable)
{
    m_pcapEnabled = enable;
}

void DiagnosticManager::writePacketToPcap(const uint8_t* data, std::size_t len, bool isTx)
{
    (void)data;
    (void)len;
    (void)isTx;
    // TODO: implement PCAP writing
}

void DiagnosticManager::workerThreadFn()
{
    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            while (!m_queue.empty()) {
                const auto& ev = m_queue.front();
                persistEvent(ev);
                m_queue.pop_front();
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (m_lastPoll.time_since_epoch().count() == 0 || now - m_lastPoll >= m_pollInterval) {
            pollMetrics();
            m_lastPoll = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DiagnosticManager::rotateLogIfNeeded()
{
    std::lock_guard<std::mutex> cfgLock(m_logCfgMtx);
    if (!m_logCfg.filePath || m_logCfg.maxFileSizeBytes == 0)
        return;

    if (!std::filesystem::exists(m_logPath))
        return;

    auto size = std::filesystem::file_size(m_logPath);
    if (size < m_logCfg.maxFileSizeBytes)
        return;

    std::filesystem::path rotated = m_logPath;
    rotated += ".1";
    if (std::filesystem::exists(rotated))
        std::filesystem::remove(rotated);
    std::filesystem::rename(m_logPath, rotated);
    m_logFile.close();
    m_logFile.open(m_logPath, std::ios::out | std::ios::trunc);
}

void DiagnosticManager::persistEvent(const Event& ev)
{
    const auto line = formatTimestamp(ev.timestamp) + " [" + severityToString(ev.severity) + "] " + ev.component + ": " + ev.message;

    {
        std::lock_guard<std::mutex> cfgLock(m_logCfgMtx);
        if (m_logCfg.logToStdout)
            std::cout << line << std::endl;

        if (m_logCfg.filePath) {
            if (!m_logFile.is_open())
                m_logFile.open(*m_logCfg.filePath, std::ios::out | std::ios::app);
            m_logFile << line << std::endl;
            m_logFile.flush();
        }
    }

    rotateLogIfNeeded();
}

void DiagnosticManager::pollMetrics()
{
    MetricsSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.threads.diagThreadRunning = m_running.load();
    snapshot.threads.pdThreadRunning = m_pd.isRunning();
    snapshot.threads.mdThreadRunning = m_md.isRunning();
    snapshot.threads.trdpThreadRunning = m_ctx.running;

    for (const auto& pdPtr : m_ctx.pdTelegrams) {
        if (!pdPtr)
            continue;
        std::lock_guard<std::mutex> lk(pdPtr->mtx);
        snapshot.pd.telegrams++;
        snapshot.pd.txCount += pdPtr->stats.txCount;
        snapshot.pd.rxCount += pdPtr->stats.rxCount;
        snapshot.pd.timeoutCount += pdPtr->stats.timeoutCount;
        snapshot.pd.maxCycleJitterUs = std::max(snapshot.pd.maxCycleJitterUs, pdPtr->stats.lastCycleJitterUs);
    }

    m_md.forEachSession([&snapshot](const engine::md::MdSessionRuntime& sess) {
        snapshot.md.sessions++;
        snapshot.md.txCount += sess.stats.txCount;
        snapshot.md.rxCount += sess.stats.rxCount;
        snapshot.md.retryCount += sess.stats.retryCount;
        snapshot.md.timeoutCount += sess.stats.timeoutCount;
        if (sess.stats.lastRxTime >= sess.stats.lastTxTime && sess.stats.lastTxTime.time_since_epoch().count() != 0) {
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(sess.stats.lastRxTime - sess.stats.lastTxTime);
            snapshot.md.maxLatencyUs = std::max(snapshot.md.maxLatencyUs, static_cast<double>(latency.count()));
        }
    });

    auto trdpErrors = m_adapter.getErrorCounters();
    snapshot.trdp.initErrors = trdpErrors.initErrors;
    snapshot.trdp.publishErrors = trdpErrors.publishErrors;
    snapshot.trdp.subscribeErrors = trdpErrors.subscribeErrors;
    snapshot.trdp.pdSendErrors = trdpErrors.pdSendErrors;
    snapshot.trdp.mdRequestErrors = trdpErrors.mdRequestErrors;
    snapshot.trdp.mdReplyErrors = trdpErrors.mdReplyErrors;
    snapshot.trdp.eventLoopErrors = trdpErrors.eventLoopErrors;
    snapshot.trdp.lastErrorCode = m_adapter.getLastErrorCode();

    {
        std::lock_guard<std::mutex> lk(m_metricsMtx);
        m_metrics = snapshot;
    }

    std::ostringstream oss;
    oss << "threads(pd=" << snapshot.threads.pdThreadRunning
        << ", md=" << snapshot.threads.mdThreadRunning
        << ", diag=" << snapshot.threads.diagThreadRunning
        << ", trdp=" << snapshot.threads.trdpThreadRunning
        << ") pd(tx=" << snapshot.pd.txCount << ", rx=" << snapshot.pd.rxCount
        << ", timeout=" << snapshot.pd.timeoutCount << ", jitter(us)=" << snapshot.pd.maxCycleJitterUs
        << ") md(tx=" << snapshot.md.txCount << ", rx=" << snapshot.md.rxCount
        << ", timeout=" << snapshot.md.timeoutCount << ", retry=" << snapshot.md.retryCount
        << ", lat(us)=" << snapshot.md.maxLatencyUs
        << ") trdp(errors=" << snapshot.trdp.eventLoopErrors << ")";
    log(Severity::DEBUG, "Diagnostics", oss.str());
}

bool DiagnosticManager::shouldLog(Severity sev) const
{
    std::lock_guard<std::mutex> lk(m_logCfgMtx);
    return static_cast<int>(sev) >= static_cast<int>(m_logCfg.minimumSeverity);
}

std::string DiagnosticManager::severityToString(Severity sev) const
{
    switch (sev) {
    case Severity::DEBUG: return "DEBUG";
    case Severity::INFO: return "INFO";
    case Severity::WARN: return "WARN";
    case Severity::ERROR: return "ERROR";
    case Severity::FATAL: return "FATAL";
    }
    return "UNKNOWN";
}

std::string DiagnosticManager::formatTimestamp(const std::chrono::system_clock::time_point& tp) const
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmStruct {};
#if defined(_WIN32)
    localtime_s(&tmStruct, &t);
#else
    localtime_r(&t, &tmStruct);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmStruct, "%F %T");
    return oss.str();
}

} // namespace diag
