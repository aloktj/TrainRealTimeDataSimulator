#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "data_types.hpp"
#include "engine_context.hpp"

namespace trdp_sim::util {

std::size_t elementSize(const data::ElementDef& def, const EngineContext& ctx);

std::vector<uint8_t> marshalDataSet(const data::DataSetInstance& inst, const EngineContext& ctx);

void unmarshalDataToDataSet(data::DataSetInstance& inst, const EngineContext& ctx, const uint8_t* data, std::size_t len);

} // namespace trdp_sim::util
