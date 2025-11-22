#include "engine_context.hpp"
#include "config_manager.hpp"
#include "trdp_adapter.hpp"
#include "pd_engine.hpp"
#include "md_engine.hpp"
#include "diagnostic_manager.hpp"
#include "backend_api.hpp"

// TODO: include Drogon initialization when you wire web server

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    trdp_sim::EngineContext ctx;

    config::ConfigManager cfgMgr;
    ctx.deviceConfig = cfgMgr.loadDeviceConfigFromXml("config/trdp.xml");
    cfgMgr.validateDeviceConfig(ctx.deviceConfig);

    // Build dataset defs & instances
    auto defs = cfgMgr.buildDataSetDefs(ctx.deviceConfig);
    for (auto& def : defs) {
        ctx.dataSetDefs[def.id] = def;
        auto inst = std::make_unique<data::DataSetInstance>();
        inst->def = &ctx.dataSetDefs[def.id];
        inst->values.resize(def.elements.size());
        ctx.dataSetInstances[def.id] = std::move(inst);
    }

    diag::DiagnosticManager diagMgr;
    diagMgr.start();

    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    adapter.init();

    engine::pd::PdEngine pdEngine(ctx, adapter);
    engine::md::MdEngine mdEngine(ctx, adapter);

    ctx.pdEngine = &pdEngine;
    ctx.mdEngine = &mdEngine;

    pdEngine.initializeFromConfig();
    mdEngine.initializeFromConfig();

    pdEngine.start();
    mdEngine.start();

    api::BackendApi api(ctx, pdEngine, mdEngine, diagMgr);

    // TODO: start Drogon web server and register controllers using 'api'

    // For now, just sleep:
    for (;;) {
        adapter.processOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup (unreachable in this simple loop)
    pdEngine.stop();
    mdEngine.stop();
    adapter.deinit();
    diagMgr.stop();

    return 0;
}
