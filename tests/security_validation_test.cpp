#include <gtest/gtest.h>

#include "auth_manager.hpp"
#include "backend_api.hpp"
#include "backend_engine.hpp"
#include "config_manager.hpp"
#include "diagnostic_manager.hpp"
#include "engine_context.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

namespace
{
    trdp_sim::EngineContext buildContextFromConfig()
    {
        config::ConfigManager   mgr;
        trdp_sim::EngineContext ctx;
        const auto              configPath =
            std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "sample_ci_device.xml";
        ctx.deviceConfig = mgr.loadDeviceConfigFromXml(configPath.string());
        mgr.validateDeviceConfig(ctx.deviceConfig);

        auto defs = mgr.buildDataSetDefs(ctx.deviceConfig);
        for (auto& def : defs)
        {
            ctx.dataSetDefs[def.id] = def;
            auto inst               = std::make_unique<data::DataSetInstance>();
            inst->def               = &ctx.dataSetDefs[def.id];
            inst->values.resize(def.elements.size());
            ctx.dataSetInstances[def.id] = std::move(inst);
        }
        return ctx;
    }
}

TEST(SecurityValidation, AuthManagerHashesPasswords)
{
    auth::AuthManager mgr;
    EXPECT_TRUE(mgr.isPasswordHashOpaque("admin", "admin123"));
    auto session = mgr.login("admin", "admin123");
    EXPECT_TRUE(session.has_value());
    EXPECT_FALSE(mgr.login("admin", "wrongpass"));
}

TEST(SecurityValidation, DatasetWritesBounded)
{
    auto ctx = buildContextFromConfig();
    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    engine::pd::PdEngine        pd(ctx, adapter);
    engine::md::MdEngine        md(ctx, adapter);
    diag::DiagnosticManager     diagMgr(ctx, pd, md, adapter, {}, {});
    trdp_sim::BackendEngine     backend(ctx, pd, md, diagMgr);

    ctx.pdEngine    = &pd;
    ctx.mdEngine    = &md;
    ctx.diagManager = &diagMgr;

    pd.initializeFromConfig();
    md.initializeFromConfig();
    backend.applyPreloadedConfiguration(ctx.deviceConfig);

    api::BackendApi api(ctx, backend, pd, md, adapter, diagMgr);

    std::vector<uint8_t> huge(70000u, 0xAA);
    std::string          error;
    EXPECT_FALSE(api.setDataSetValue(2, 0, huge, &error));
    EXPECT_FALSE(error.empty());

    auto expected = api.getExpectedElementSize(2, 0);
    ASSERT_TRUE(expected.has_value());
    std::vector<uint8_t> ok(*expected, 0xBB);
    EXPECT_TRUE(api.setDataSetValue(2, 0, ok, &error));
}

TEST(SecurityValidation, MdTcpDispatchIsRateLimited)
{
    auto ctx = buildContextFromConfig();
    if (!ctx.deviceConfig.interfaces.empty())
        ctx.deviceConfig.interfaces[0].mdCom.protocol = config::MdComParameter::Protocol::TCP;

    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    engine::pd::PdEngine        pd(ctx, adapter);
    engine::md::MdEngine        md(ctx, adapter);

    ctx.pdEngine = &pd;
    ctx.mdEngine = &md;

    pd.initializeFromConfig();
    md.initializeFromConfig();

    auto sessionId = md.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);

    auto begin = std::chrono::steady_clock::now();
    md.sendRequest(sessionId);

    auto opt = md.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    {
        std::lock_guard<std::mutex> lk((*opt)->mtx);
        (*opt)->state = engine::md::MdSessionState::IDLE;
        (*opt)->proto = engine::md::MdProtocol::TCP;
    }
    md.sendRequest(sessionId);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin);
    EXPECT_GE(elapsed.count(), 50);
}
