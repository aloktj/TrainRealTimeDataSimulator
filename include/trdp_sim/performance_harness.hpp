#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace trdp_sim::perf
{

    enum class Platform
    {
        VM,
        RaspberryPi
    };

    struct Thresholds
    {
        double jitterVmMicros{1000.0};
        double jitterPiMicros{5000.0};
        std::size_t minMdConcurrency{200};
        std::size_t minPdTelegrams{500};
        double      minWebUiHz{10.0};
    };

    struct PerformanceReport
    {
        double      pdJitterMicros{0.0};
        std::size_t pdTelegramCount{0};
        std::size_t mdPeakConcurrency{0};
        double      webUiUpdateRateHz{0.0};
        double      durationSeconds{0.0};

        std::string toJson() const;
    };

    class BenchmarkHarness
    {
      public:
        using Clock      = std::chrono::steady_clock;
        using TimePoint  = Clock::time_point;

        void recordPdTelegram(TimePoint ts);
        void recordWebUiUpdate(TimePoint ts);
        void mdSessionStarted();
        void mdSessionFinished();

        PerformanceReport snapshot() const;
        bool              meetsThresholds(Platform platform, const Thresholds& thresholds) const;

      private:
        std::vector<TimePoint> m_pdEvents;
        std::vector<TimePoint> m_webUiUpdates;
        std::size_t            m_mdActive{0};
        std::size_t            m_mdPeak{0};
        std::optional<TimePoint> m_start;
        std::optional<TimePoint> m_lastWebUpdate;
    };

} // namespace trdp_sim::perf

