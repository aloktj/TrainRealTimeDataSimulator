#include "data_marshalling.hpp"

namespace trdp_sim::util {

namespace {
std::size_t elementTypeSize(data::ElementType type)
{
    switch (type) {
    case data::ElementType::BOOL8:
    case data::ElementType::CHAR8:
    case data::ElementType::INT8:
    case data::ElementType::UINT8:
        return 1;
    case data::ElementType::UTF16:
    case data::ElementType::INT16:
    case data::ElementType::UINT16:
        return 2;
    case data::ElementType::INT32:
    case data::ElementType::UINT32:
    case data::ElementType::REAL32:
    case data::ElementType::TIMEDATE32:
        return 4;
    case data::ElementType::INT64:
    case data::ElementType::UINT64:
    case data::ElementType::REAL64:
    case data::ElementType::TIMEDATE64:
        return 8;
    case data::ElementType::TIMEDATE48:
        return 6;
    case data::ElementType::NESTED_DATASET:
        return 0; // Determined dynamically
    }
    return 0;
}
} // namespace

std::size_t elementSize(const data::ElementDef& def, const EngineContext& ctx)
{
    if (def.type == data::ElementType::NESTED_DATASET) {
        if (!def.nestedDataSetId)
            return 0;
        auto it = ctx.dataSetDefs.find(*def.nestedDataSetId);
        if (it == ctx.dataSetDefs.end())
            return 0;
        std::size_t nestedSize = 0;
        for (const auto& nestedEl : it->second.elements)
            nestedSize += elementSize(nestedEl, ctx);
        return nestedSize * def.arraySize;
    }
    return elementTypeSize(def.type) * def.arraySize;
}

std::vector<uint8_t> marshalDataSet(const data::DataSetInstance& inst, const EngineContext& ctx)
{
    std::vector<uint8_t> out;
    if (!inst.def)
        return out;

    for (std::size_t idx = 0; idx < inst.def->elements.size() && idx < inst.values.size(); ++idx) {
        const auto& el = inst.def->elements[idx];
        const auto& cell = inst.values[idx];
        const auto expectedSize = elementSize(el, ctx);
        if (!cell.defined) {
            out.insert(out.end(), expectedSize, 0);
            continue;
        }

        if (cell.raw.size() >= expectedSize) {
            out.insert(out.end(), cell.raw.begin(), cell.raw.begin() + expectedSize);
        } else {
            out.insert(out.end(), cell.raw.begin(), cell.raw.end());
            out.insert(out.end(), expectedSize - cell.raw.size(), 0);
        }
    }

    return out;
}

void unmarshalDataToDataSet(data::DataSetInstance& inst, const EngineContext& ctx, const uint8_t* data, std::size_t len)
{
    if (!inst.def)
        return;

    std::size_t offset = 0;
    for (std::size_t idx = 0; idx < inst.def->elements.size() && idx < inst.values.size(); ++idx) {
        const auto& el = inst.def->elements[idx];
        auto& cell = inst.values[idx];
        const auto expectedSize = elementSize(el, ctx);
        if (expectedSize == 0)
            continue;

        if (offset >= len) {
            cell.raw.assign(expectedSize, 0);
            cell.defined = false;
            continue;
        }

        auto remaining = len - offset;
        auto toCopy = std::min<std::size_t>(expectedSize, remaining);
        cell.raw.assign(data + offset, data + offset + toCopy);
        if (toCopy < expectedSize)
            cell.raw.resize(expectedSize, 0);
        cell.defined = true;
        offset += expectedSize;
    }
}

} // namespace trdp_sim::util
