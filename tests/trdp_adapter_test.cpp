#include <gtest/gtest.h>

#include "config_manager.hpp"
#include "md_engine.hpp"
#include "pd_engine.hpp"
#include "trdp_adapter.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <memory>
#include <thread>

namespace {

trdp_sim::EngineContext buildContextFromSample()
{
    config::ConfigManager mgr;
    trdp_sim::EngineContext ctx;
    const auto configPath = std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "sample_ci_device.xml";
    ctx.deviceConfig = mgr.loadDeviceConfigFromXml(configPath.string());
    mgr.validateDeviceConfig(ctx.deviceConfig);

    auto defs = mgr.buildDataSetDefs(ctx.deviceConfig);
    for (auto& def : defs) {
        ctx.dataSetDefs[def.id] = def;
        auto inst = std::make_unique<data::DataSetInstance>();
        inst->def = &ctx.dataSetDefs[def.id];
        inst->values.resize(def.elements.size());
        ctx.dataSetInstances[def.id] = std::move(inst);
    }
    return ctx;
}

} // namespace

TEST(TrdpAdapterTest, RecordsErrorsWhenSendFails)
{
    trdp_sim::EngineContext ctx;
    trdp_sim::trdp::TrdpAdapter adapter(ctx);

    engine::pd::PdTelegramRuntime pdRt {};
    adapter.setPdSendResult(-2);
    EXPECT_EQ(adapter.sendPdData(pdRt, {0xAA}), -2);

    auto counters = adapter.getErrorCounters();
    EXPECT_EQ(counters.pdSendErrors, 1u);
    ASSERT_TRUE(adapter.getLastErrorCode().has_value());
    EXPECT_EQ(adapter.getLastErrorCode().value(), 2u);
}

class TrdpAdapterEngineHarness : public ::testing::Test {
protected:
    TrdpAdapterEngineHarness()
        : ctx(buildContextFromSample())
        , adapter(ctx)
        , pdEngine(ctx, adapter)
        , mdEngine(ctx, adapter)
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

    trdp_sim::EngineContext ctx;
    trdp_sim::trdp::TrdpAdapter adapter;
    engine::pd::PdEngine pdEngine;
    engine::md::MdEngine mdEngine;
};

TEST_F(TrdpAdapterEngineHarness, PdPublishingMarshalsDataset)
{
    auto* ds = pdEngine.getDataSetInstance(1);
    ASSERT_NE(ds, nullptr);

    {
        std::lock_guard<std::mutex> lk(ds->mtx);
        ds->values[0].defined = true;
        ds->values[0].raw = {0x34, 0x12};
        ds->values[1].defined = true;
        ds->values[1].raw = {1};
    }

    pdEngine.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    pdEngine.stop();

    auto payload = adapter.getLastPdPayload();
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_EQ(payload[0], 0x34);
    EXPECT_EQ(payload[1], 0x12);
    EXPECT_EQ(payload[2], 0x01);
}

TEST_F(TrdpAdapterEngineHarness, CallbacksPopulateDatasets)
{
    auto* inboundDs = pdEngine.getDataSetInstance(3);
    ASSERT_NE(inboundDs, nullptr);

    const std::array<uint8_t, 8> pdPayload {1, 0, 0, 0, 'T', 'E', 'L', 'E'};
    adapter.handlePdCallback(3001, pdPayload.data(), pdPayload.size());

    {
        std::lock_guard<std::mutex> lk(inboundDs->mtx);
        ASSERT_EQ(inboundDs->values.size(), 2u);
        EXPECT_TRUE(inboundDs->values[0].defined);
        EXPECT_TRUE(inboundDs->values[1].defined);
        EXPECT_EQ(inboundDs->values[1].raw[0], 'T');
    }

    auto sessionId = mdEngine.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);
    const std::array<uint8_t, 5> mdPayload {0x07, 0xEF, 0xBE, 0xAD, 0xDE};
    adapter.handleMdCallback(sessionId, mdPayload.data(), mdPayload.size());

    auto opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    auto* session = *opt;
    ASSERT_NE(session->responseData, nullptr);
    std::lock_guard<std::mutex> dsLock(session->responseData->mtx);
    EXPECT_TRUE(session->responseData->values[0].defined);
    EXPECT_TRUE(session->responseData->values[1].defined);
    EXPECT_EQ(session->responseData->values[0].raw[0], 0x07);
}

TEST_F(TrdpAdapterEngineHarness, MdRequestFailureSetsError)
{
    adapter.setMdRequestResult(-5);
    auto sessionId = mdEngine.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);

    mdEngine.sendRequest(sessionId);

    auto opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    std::lock_guard<std::mutex> lk((*opt)->mtx);
    EXPECT_EQ((*opt)->state, engine::md::MdSessionState::ERROR);

    auto counters = adapter.getErrorCounters();
    EXPECT_EQ(counters.mdRequestErrors, 1u);
}
