#include "performance_harness.hpp"

#include <cmath>
#include <numeric>
#include <sstream>

namespace trdp_sim::perf
{

    namespace
    {

        double computeJitterMicros(const std::vector<BenchmarkHarness::TimePoint>& events)
        {
            if (events.size() < 2)
                return 0.0;

            std::vector<double> intervals;
            intervals.reserve(events.size() - 1);
            for (std::size_t i = 1; i < events.size(); ++i)
            {
                const auto delta = std::chrono::duration_cast<std::chrono::duration<double>>(events[i] - events[i - 1]);
                intervals.push_back(delta.count());
            }

            const double mean = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
            double       maxJitter{0.0};
            for (double interval : intervals)
            {
                maxJitter = std::max(maxJitter, std::abs(interval - mean));
            }
            return maxJitter * 1'000'000.0;
        }

        double computeRateHz(const std::vector<BenchmarkHarness::TimePoint>& events)
        {
            if (events.size() < 2)
                return 0.0;

            const auto total = std::chrono::duration_cast<std::chrono::duration<double>>(events.back() - events.front());
            if (total.count() <= 0.0)
                return 0.0;
            return static_cast<double>(events.size() - 1) / total.count();
        }

    } // namespace

    std::string PerformanceReport::toJson() const
    {
        std::ostringstream oss;
        oss << "{\"pdJitterMicros\":" << pdJitterMicros << ",\"pdTelegramCount\":" << pdTelegramCount
            << ",\"mdPeakConcurrency\":" << mdPeakConcurrency << ",\"webUiUpdateRateHz\":" << webUiUpdateRateHz
            << ",\"durationSeconds\":" << durationSeconds << "}";
        return oss.str();
    }

    void BenchmarkHarness::recordPdTelegram(TimePoint ts)
    {
        if (!m_start)
            m_start = ts;
        m_pdEvents.push_back(ts);
    }

    void BenchmarkHarness::recordWebUiUpdate(TimePoint ts)
    {
        if (!m_start)
            m_start = ts;
        m_webUiUpdates.push_back(ts);
        m_lastWebUpdate = ts;
    }

    void BenchmarkHarness::mdSessionStarted()
    {
        ++m_mdActive;
        if (m_mdActive > m_mdPeak)
            m_mdPeak = m_mdActive;
    }

    void BenchmarkHarness::mdSessionFinished()
    {
        if (m_mdActive > 0)
            --m_mdActive;
    }

    PerformanceReport BenchmarkHarness::snapshot() const
    {
        PerformanceReport report{};
        report.pdTelegramCount   = m_pdEvents.size();
        report.mdPeakConcurrency = m_mdPeak;
        report.pdJitterMicros    = computeJitterMicros(m_pdEvents);
        report.webUiUpdateRateHz = computeRateHz(m_webUiUpdates);
        if (m_start && !m_pdEvents.empty())
        {
            const auto endTime = m_lastWebUpdate ? *m_lastWebUpdate : m_pdEvents.back();
            report.durationSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(endTime - *m_start).count();
        }
        return report;
    }

    bool BenchmarkHarness::meetsThresholds(Platform platform, const Thresholds& thresholds) const
    {
        const auto report = snapshot();
        const auto jitter = platform == Platform::VM ? thresholds.jitterVmMicros : thresholds.jitterPiMicros;
        return report.pdTelegramCount >= thresholds.minPdTelegrams && report.mdPeakConcurrency >= thresholds.minMdConcurrency &&
               report.pdJitterMicros <= jitter && report.webUiUpdateRateHz >= thresholds.minWebUiHz;
    }

} // namespace trdp_sim::perf

