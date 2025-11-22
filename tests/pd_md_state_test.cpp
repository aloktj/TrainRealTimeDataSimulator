#include <gtest/gtest.h>

#include "config_manager.hpp"
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

} // namespace

class PdMdStateTest : public ::testing::Test
{
  protected:
    PdMdStateTest() : ctx(buildContextFromConfig()), adapter(ctx), pdEngine(ctx, adapter), mdEngine(ctx, adapter)
    {
        ctx.pdEngine = &pdEngine;
        ctx.mdEngine = &mdEngine;
    }

    void SetUp() override
    {
        pdEngine.initializeFromConfig();
        mdEngine.initializeFromConfig();
    }

    void TearDown() override
    {
        pdEngine.stop();
        mdEngine.stop();
    }

    trdp_sim::EngineContext     ctx;
    trdp_sim::trdp::TrdpAdapter adapter;
    engine::pd::PdEngine        pdEngine;
    engine::md::MdEngine        mdEngine;
};

TEST_F(PdMdStateTest, PdReceiveUpdatesDataset)
{
    auto* ds = pdEngine.getDataSetInstance(3);
    ASSERT_NE(ds, nullptr);

    const std::array<uint8_t, 8> payload{1, 0, 0, 0, 'T', 'E', 'S', 'T'};
    adapter.handlePdCallback(3001, payload.data(), payload.size());

    std::lock_guard<std::mutex> lk(ds->mtx);
    ASSERT_EQ(ds->values.size(), 2u);
    EXPECT_TRUE(ds->values[0].defined);
    EXPECT_TRUE(ds->values[1].defined);
    EXPECT_EQ(ds->values[1].raw[0], 'T');
}

TEST_F(PdMdStateTest, MdSessionTimesOutAndTracksRetries)
{
    mdEngine.start();
    auto sessionId = mdEngine.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);

    mdEngine.sendRequest(sessionId);

    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    auto opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    auto* session = *opt;
    {
        std::lock_guard<std::mutex> lk(session->mtx);
        EXPECT_TRUE(session->state == engine::md::MdSessionState::WAITING_REPLY ||
                    session->state == engine::md::MdSessionState::TIMEOUT);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    session = *opt;
    {
        std::lock_guard<std::mutex> lk(session->mtx);
        EXPECT_EQ(session->state, engine::md::MdSessionState::TIMEOUT);
        EXPECT_GE(session->stats.timeoutCount, 1u);
    }

    const std::array<uint8_t, 2> reply{0xAA, 0xBB};
    TRDP_MD_INFO_T info{};
    info.sessionId = sessionId;
    info.comId     = 2001;
    adapter.handleMdCallback(&info, reply.data(), reply.size());
    opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    session = *opt;
    {
        std::lock_guard<std::mutex> lk(session->mtx);
        EXPECT_EQ(session->state, engine::md::MdSessionState::REPLY_RECEIVED);
        EXPECT_GE(session->stats.rxCount, 1u);
    }
}
