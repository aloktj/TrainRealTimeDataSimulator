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

    namespace
    {
        bool ensureAdapterInitialized(EngineContext& ctx)
        {
            if (!ctx.trdpAdapter)
                return false;
            if (ctx.trdpSession)
                return true;
            return ctx.trdpAdapter->init();
        }
    } // namespace

    void BackendEngine::applyConfiguration(const config::DeviceConfig& cfg, bool activateTransport)
    {
        m_pd.stop();
        m_md.stop();

        if (!activateTransport && m_ctx.trdpAdapter)
            m_ctx.trdpAdapter->deinit();

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

        m_pd.initializeFromConfig(activateTransport);
        m_md.initializeFromConfig();

        if (activateTransport)
        {
            if (ensureAdapterInitialized(m_ctx))
            {
                m_pd.start();
                m_md.start();
                m_ctx.transportActive = true;
            }
            else
            {
                m_ctx.transportActive = false;
            }
        }
        else
        {
            m_ctx.transportActive = false;
        }
    }

    void BackendEngine::loadConfiguration(const std::string& path)
    {
        auto cfg = m_loader.load(path);
        applyConfiguration(cfg, m_ctx.transportActive);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration loaded from " + path);
    }

    void BackendEngine::reloadConfiguration(const std::string& path)
    {
        auto cfg = m_loader.load(path);
        applyConfiguration(cfg, m_ctx.transportActive);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration reloaded from " + path);
    }

    void BackendEngine::applyPreloadedConfiguration(const config::DeviceConfig& cfg, bool activateTransport)
    {
        applyConfiguration(cfg, activateTransport);
        m_diag.log(diag::Severity::INFO, "BackendEngine", "Configuration applied from memory");
    }

    bool BackendEngine::startTransport()
    {
        if (m_ctx.transportActive)
            return true;

        if (!ensureAdapterInitialized(m_ctx))
        {
            m_diag.log(diag::Severity::ERROR, "BackendEngine", "Failed to initialize TRDP adapter");
            return false;
        }

        m_pd.initializeFromConfig(true);
        m_md.initializeFromConfig();
        m_pd.start();
        m_md.start();
        m_ctx.transportActive = true;
        m_diag.log(diag::Severity::INFO, "BackendEngine", "TRDP transport started by user request");
        return true;
    }

    void BackendEngine::stopTransport()
    {
        if (!m_ctx.transportActive)
            return;

        m_pd.stop();
        m_md.stop();
        if (m_ctx.trdpAdapter)
            m_ctx.trdpAdapter->deinit();
        m_ctx.transportActive = false;
        m_diag.log(diag::Severity::INFO, "BackendEngine", "TRDP transport stopped by user request");
    }

} // namespace trdp_sim

