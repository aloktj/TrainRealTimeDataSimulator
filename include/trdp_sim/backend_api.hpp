#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "diagnostic_manager.hpp"
#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"

// forward-declare nlohmann::json
#include <nlohmann/json_fwd.hpp>

namespace api
{

    class BackendApi
    {
      public:
        BackendApi(trdp_sim::EngineContext& ctx, engine::pd::PdEngine& pd, engine::md::MdEngine& md,
                   diag::DiagnosticManager& diag);

        // PD related:
        nlohmann::json getPdStatus() const;
        void           enablePdTelegram(uint32_t comId, bool enable);
        nlohmann::json getDataSetValues(uint32_t dataSetId) const;
        void           setDataSetValue(uint32_t dataSetId, std::size_t elementIdx, const std::vector<uint8_t>& value);
        void           clearDataSetValue(uint32_t dataSetId, std::size_t elementIdx);
        void           clearAllDataSetValues(uint32_t dataSetId);
        void           lockDataSet(uint32_t dataSetId, bool lock);

        // MD related:
        uint32_t       createMdRequest(uint32_t comId);
        void           sendMdRequest(uint32_t sessionId);
        nlohmann::json getMdSessionStatus(uint32_t sessionId) const;

        // Config and control:
        void           reloadConfiguration(const std::string& xmlPath);
        nlohmann::json getConfigSummary() const;

        // Diagnostics:
        nlohmann::json getRecentEvents(std::size_t maxEvents) const;
        void           triggerDiagnosticEvent(const std::string& severity, const std::string& component,
                                              const std::string&                message,
                                              const std::optional<std::string>& extraJson = std::nullopt);
        void           enablePcap(bool enable);
        nlohmann::json getDiagnosticsMetrics() const;

      private:
        trdp_sim::EngineContext& m_ctx;
        engine::pd::PdEngine&    m_pd;
        engine::md::MdEngine&    m_md;
        diag::DiagnosticManager& m_diag;
    };

} // namespace api
