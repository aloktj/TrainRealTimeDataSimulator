#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

    namespace trdp = trdp_sim::trdp;

    class BackendApi
    {
      public:
        BackendApi(trdp_sim::EngineContext& ctx, trdp_sim::BackendEngine& backend, engine::pd::PdEngine& pd,
                   engine::md::MdEngine& md, trdp::TrdpAdapter& trdpAdapter, diag::DiagnosticManager& diag);

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
        nlohmann::json getMulticastStatus() const;
        bool           joinMulticastGroup(const std::string& ifaceName, const std::string& group,
                                          const std::optional<std::string>& nic);
        bool           leaveMulticastGroup(const std::string& ifaceName, const std::string& group);

        // Diagnostics:
        nlohmann::json getRecentEvents(std::size_t maxEvents) const;
        std::string    exportRecentEventsText(std::size_t maxEvents) const;
        void           triggerDiagnosticEvent(const std::string& severity, const std::string& component,
                                              const std::string&                message,
                                              const std::optional<std::string>& extraJson = std::nullopt);
        void           enablePcap(bool enable);
        nlohmann::json getDiagnosticsMetrics() const;
        std::optional<std::filesystem::path> getPcapCapturePath() const;
        std::optional<std::filesystem::path> getLogFilePath() const;
        std::optional<std::filesystem::path> getConfigPath() const;
        bool                                backupConfiguration(const std::filesystem::path& destination) const;
        bool                                restoreConfiguration(const std::filesystem::path& source);

        // Simulation controls
        nlohmann::json getSimulationState() const;
        void           upsertPdInjectionRule(uint32_t comId, const trdp_sim::SimulationControls::InjectionRule& rule);
        void           upsertMdInjectionRule(uint32_t comId, const trdp_sim::SimulationControls::InjectionRule& rule);
        void           upsertDataSetInjectionRule(uint32_t dataSetId, const trdp_sim::SimulationControls::InjectionRule& rule);
        void           clearInjectionRules();
        void           setStressMode(const trdp_sim::SimulationControls::StressMode& stress);
        void           setRedundancySimulation(const trdp_sim::SimulationControls::RedundancySimulation& sim);
        void           setTimeSyncOffsets(const trdp_sim::SimulationControls::TimeSyncOffsets& offsets);
        bool           registerVirtualInstance(const std::string& name, const std::string& path, std::string* err = nullptr);
        bool           activateVirtualInstance(const std::string& name, std::string* err = nullptr);
        nlohmann::json listVirtualInstances() const;

      private:
        trdp_sim::EngineContext& m_ctx;
        engine::pd::PdEngine&    m_pd;
        engine::md::MdEngine&    m_md;
        diag::DiagnosticManager& m_diag;
        trdp_sim::BackendEngine& m_backend;
        trdp::TrdpAdapter&       m_trdp;
    };

} // namespace api
