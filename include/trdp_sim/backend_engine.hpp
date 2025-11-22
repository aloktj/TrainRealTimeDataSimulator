#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "config_manager.hpp"
#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "xml_loader.hpp"

namespace diag
{
    class DiagnosticManager;
}

namespace trdp_sim
{

    /**
     * TRDP backend engine responsible for loading configuration, wiring
     * parsed definitions into the runtime context, and coordinating
     * PD/MD engine lifecycle during reloads.
     */
    class BackendEngine
    {
      public:
        BackendEngine(EngineContext& ctx, engine::pd::PdEngine& pd, engine::md::MdEngine& md,
                      diag::DiagnosticManager& diag);

        void loadConfiguration(const std::string& path);
        void reloadConfiguration(const std::string& path);
        void applyPreloadedConfiguration(const config::DeviceConfig& cfg);

        const config::DeviceConfig& deviceConfig() const { return m_ctx.deviceConfig; }

      private:
        void applyConfiguration(const config::DeviceConfig& cfg);
        void rebuildDataSets(const config::DeviceConfig& cfg);

        EngineContext&             m_ctx;
        engine::pd::PdEngine&      m_pd;
        engine::md::MdEngine&      m_md;
        diag::DiagnosticManager&   m_diag;
        config::XmlConfigurationLoader m_loader;
    };

} // namespace trdp_sim

