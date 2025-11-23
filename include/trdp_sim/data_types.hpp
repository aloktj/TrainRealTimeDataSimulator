#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace data
{

    enum class ElementType : uint8_t
    {
        BOOL8 = 1,
        CHAR8,
        UTF16,
        INT8,
        INT16,
        INT32,
        INT64,
        UINT8,
        UINT16,
        UINT32,
        UINT64,
        REAL32,
        REAL64,
        TIME_DATE32,
        TIME_DATE48,
        TIME_DATE64,
        NESTED_DATASET
    };

    struct ElementDef
    {
        std::string             name;
        ElementType             type{ElementType::UINT8};
        uint32_t                arraySize{1};
        std::optional<uint32_t> nestedDataSetId;
    };

    struct DataSetDef
    {
        uint32_t                id{};
        std::string             name;
        std::vector<ElementDef> elements;
    };

    struct ValueCell
    {
        bool                 defined{false};
        std::vector<uint8_t> raw; // marshalled/host repr
    };

    struct DataSetInstance
    {
        const DataSetDef*      def{nullptr};
        std::vector<ValueCell> values;
        bool                   locked{false};
        bool                   isOutgoing{false};
        mutable std::mutex     mtx;
    };

} // namespace data
