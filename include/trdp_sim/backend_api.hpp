#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "diagnostic_manager.hpp"
#include "engine_context.hpp"
#include "backend_engine.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"

// forward-declare nlohmann::json
#include <nlohmann/json_fwd.hpp>

namespace api
{

    class BackendApi
    {
      public:
        BackendApi(trdp_sim::EngineContext& ctx, trdp_sim::BackendEngine& backend, engine::pd::PdEngine& pd,
                   engine::md::MdEngine& md, diag::DiagnosticManager& diag);

        // PD related:
        nlohmann::json getPdStatus() const;
        void           enablePdTelegram(uint32_t comId, bool enable);
        nlohmann::json getDataSetValues(uint32_t dataSetId) const;
        bool           setDataSetValue(uint32_t dataSetId, std::size_t elementIdx, const std::vector<uint8_t>& value,
                                       std::string* error = nullptr);
        bool clearDataSetValue(uint32_t dataSetId, std::size_t elementIdx, std::string* error = nullptr);
        bool clearAllDataSetValues(uint32_t dataSetId, std::string* error = nullptr);
        bool lockDataSet(uint32_t dataSetId, bool lock, std::string* error = nullptr);

        // MD related:
        uint32_t       createMdRequest(uint32_t comId);
        void           sendMdRequest(uint32_t sessionId);
        nlohmann::json getMdSessionStatus(uint32_t sessionId) const;

        // Config and control:
        void           reloadConfiguration(const std::string& xmlPath);
        nlohmann::json getConfigSummary() const;
        nlohmann::json getConfigDetail() const;

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
        trdp_sim::BackendEngine& m_backend;
    };

} // namespace api
