#include <gtest/gtest.h>

#include "data_marshalling.hpp"

#include <array>

using trdp_sim::EngineContext;
using trdp_sim::util::marshalDataSet;
using trdp_sim::util::unmarshalDataToDataSet;

TEST(DataMarshalling, MarshalsAndUnmarshalsWithPadding)
{
    EngineContext ctx;
    data::DataSetDef def;
    def.id = 1;
    def.name = "Test";
    def.elements = {{"a", data::ElementType::UINT16, 1, std::nullopt}, {"b", data::ElementType::CHAR8, 4, std::nullopt}};

    ctx.dataSetDefs[def.id] = def;

    data::DataSetInstance inst;
    inst.def = &ctx.dataSetDefs[def.id];
    inst.values.resize(def.elements.size());

    inst.values[0].defined = true;
    inst.values[0].raw = {0x34, 0x12};
    inst.values[1].defined = true;
    inst.values[1].raw = {'A', 'B'}; // shorter than expected

    auto payload = marshalDataSet(inst, ctx);
    ASSERT_EQ(payload.size(), 6u);
    EXPECT_EQ(payload[0], 0x34);
    EXPECT_EQ(payload[1], 0x12);
    EXPECT_EQ(payload[2], 'A');
    EXPECT_EQ(payload[3], 'B');
    EXPECT_EQ(payload[4], 0);
    EXPECT_EQ(payload[5], 0);

    data::DataSetInstance dest;
    dest.def = &ctx.dataSetDefs[def.id];
    dest.values.resize(def.elements.size());
    unmarshalDataToDataSet(dest, ctx, payload.data(), payload.size());

    EXPECT_TRUE(dest.values[0].defined);
    EXPECT_TRUE(dest.values[1].defined);
    EXPECT_EQ(dest.values[1].raw[2], 0);
}

TEST(DataMarshalling, UndefinedElementsAreZeroed)
{
    EngineContext ctx;
    data::DataSetDef def;
    def.id = 2;
    def.name = "Undefined";
    def.elements = {{"a", data::ElementType::UINT32, 1, std::nullopt}};
    ctx.dataSetDefs[def.id] = def;

    data::DataSetInstance inst;
    inst.def = &ctx.dataSetDefs[def.id];
    inst.values.resize(def.elements.size());

    auto payload = marshalDataSet(inst, ctx);
    ASSERT_EQ(payload.size(), 4u);
    EXPECT_EQ(payload, (std::vector<uint8_t>{0, 0, 0, 0}));
}
