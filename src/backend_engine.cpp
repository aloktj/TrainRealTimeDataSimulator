#include "backend_engine.hpp"

#include "diagnostic_manager.hpp"

#include <mutex>

namespace trdp_sim
{

    BackendEngine::BackendEngine(EngineContext& ctx, engine::pd::PdEngine& pd, engine::md::MdEngine& md,
                                 diag::DiagnosticManager& diag)
        : m_ctx(ctx), m_pd(pd), m_md(md), m_diag(diag)
    {
    }

    void BackendEngine::rebuildDataSets(const config::DeviceConfig& cfg)
    {
        std::unordered_map<uint32_t, data::DataSetDef>                       newDefs;
        std::unordered_map<uint32_t, std::unique_ptr<data::DataSetInstance>> newInsts;

        auto defs = config::ConfigManager().buildDataSetDefs(cfg);
        for (auto& def : defs)
        {
            newDefs[def.id] = def;
            auto inst       = std::make_unique<data::DataSetInstance>();
            inst->def       = &newDefs[def.id];
            inst->values.resize(def.elements.size());
            newInsts[def.id] = std::move(inst);
        }

        m_ctx.dataSetDefs      = std::move(newDefs);
        m_ctx.dataSetInstances = std::move(newInsts);
    }

    void BackendEngine::applyConfiguration(const config::DeviceConfig& cfg)
    {
        m_pd.stop();
        m_md.stop();

        m_ctx.pdTelegrams.clear();
        m_ctx.mdSessions.clear();

        m_ctx.deviceConfig = cfg;

        {
            std::lock_guard<std::mutex> lk(m_ctx.multicastMtx);
            m_ctx.multicastGroups.clear();
            for (const auto& iface : cfg.interfaces)
            {
                for (const auto& group : iface.multicastGroups)
                {
                    EngineContext::MulticastGroupState state;
                    state.ifaceName = iface.name;
                    state.address   = group.address;
                    state.nic       = group.nic;
                    state.hostIp    = iface.hostIp;
                    m_ctx.multicastGroups.push_back(std::move(state));
                }
            }
        }

        rebuildDataSets(cfg);

        m_pd.initializeFromConfig();
        m_md.initializeFromConfig();

        m_pd.start();
        m_md.start();
    }

    void BackendEngine::loadConfiguration(const std::string& path)
    {
        auto cfg = m_loader.load(path);
        applyConfiguration(cfg);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration loaded from " + path);
    }

    void BackendEngine::reloadConfiguration(const std::string& path)
    {
        auto cfg = m_loader.load(path);
        applyConfiguration(cfg);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration reloaded from " + path);
    }

    void BackendEngine::applyPreloadedConfiguration(const config::DeviceConfig& cfg)
    {
        applyConfiguration(cfg);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration applied from memory");
    }

} // namespace trdp_sim

