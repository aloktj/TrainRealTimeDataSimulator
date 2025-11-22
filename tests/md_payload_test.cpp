#include <gtest/gtest.h>

#include "config_manager.hpp"
#include "md_engine.hpp"
#include "trdp_adapter.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>

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

TEST(MdPayload, MarshalsRequestPayload)
{
    auto                        ctx = buildContextFromConfig();
    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    engine::md::MdEngine        mdEngine(ctx, adapter);
    ctx.mdEngine = &mdEngine;

    mdEngine.initializeFromConfig();

    auto dsIt = ctx.dataSetInstances.find(2);
    ASSERT_NE(dsIt, ctx.dataSetInstances.end());
    auto* inst = dsIt->second.get();
    {
        std::lock_guard<std::mutex> lk(inst->mtx);
        inst->values[0].raw     = {0x7F};
        inst->values[0].defined = true;
        inst->values[1].raw     = {0x12, 0x34, 0x56, 0x78};
        inst->values[1].defined = true;
    }

    auto sessionId = mdEngine.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);

    mdEngine.sendRequest(sessionId);

    auto payload = adapter.getLastMdRequestPayload();
    ASSERT_EQ(payload.size(), 5u);
    EXPECT_EQ(payload[0], 0x7F);
    EXPECT_EQ(payload[1], 0x12);
    EXPECT_EQ(payload[2], 0x34);
    EXPECT_EQ(payload[3], 0x56);
    EXPECT_EQ(payload[4], 0x78);
}

TEST(MdPayload, UnmarshalsTruncatedReply)
{
    auto                        ctx = buildContextFromConfig();
    trdp_sim::trdp::TrdpAdapter adapter(ctx);
    engine::md::MdEngine        mdEngine(ctx, adapter);
    ctx.mdEngine = &mdEngine;

    mdEngine.initializeFromConfig();

    auto sessionId = mdEngine.createRequestSession(2001);
    ASSERT_NE(sessionId, 0u);

    const std::array<uint8_t, 3> payload{0xAA, 0xBB, 0xCC};
    mdEngine.onMdIndication(sessionId, payload.data(), payload.size());

    auto opt = mdEngine.getSession(sessionId);
    ASSERT_TRUE(opt.has_value());
    auto* session = *opt;
    ASSERT_NE(session->responseData, nullptr);

    std::lock_guard<std::mutex> lk(session->responseData->mtx);
    ASSERT_EQ(session->responseData->values.size(), 2u);
    EXPECT_TRUE(session->responseData->values[0].defined);
    ASSERT_EQ(session->responseData->values[0].raw.size(), 1u);
    EXPECT_EQ(session->responseData->values[0].raw[0], 0xAA);

    EXPECT_TRUE(session->responseData->values[1].defined);
    ASSERT_EQ(session->responseData->values[1].raw.size(), 4u);
    EXPECT_EQ(session->responseData->values[1].raw[0], 0xBB);
    EXPECT_EQ(session->responseData->values[1].raw[1], 0xCC);
    EXPECT_EQ(session->responseData->values[1].raw[2], 0x00);
    EXPECT_EQ(session->responseData->values[1].raw[3], 0x00);
}
