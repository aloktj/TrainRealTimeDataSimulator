#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace data
{
    struct DataSetDef;
}

namespace config
{

    struct SchemaIssue
    {
        std::string message;
        int         line{0};
    };

    class ConfigError : public std::runtime_error
    {
      public:
        ConfigError(const std::string& filePath, int line, const std::string& message);

        int         line() const { return m_line; }
        const std::string& file() const { return m_file; }

      private:
        std::string m_file;
        int         m_line{};
    };

    // ---- Structs from DDS, simplified where needed ----

    struct MemBlockConfig
    {
        uint32_t size{};
        uint32_t preallocate{};
    };

    struct MemoryConfig
    {
        uint32_t                    memorySize{};
        std::vector<MemBlockConfig> blocks;
    };

    struct DebugConfig
    {
        std::string fileName;
        uint32_t    fileSize{};
        std::string info;
        char        level{'W'};
    };

    struct PcapConfig
    {
        bool        enabled{false};
        bool        captureTx{true};
        bool        captureRx{true};
        std::string fileName;
        uint32_t    maxSizeBytes{0};
        uint32_t    maxFiles{2};
    };

    struct ComParameter
    {
        uint32_t id{};
        uint8_t  qos{};
        uint8_t  ttl{};
    };

    struct TrdpProcessConfig
    {
        bool     blocking{false};
        uint32_t cycleTimeUs{};
        uint8_t  priority{};
        bool     trafficShaping{false};
    };

    struct PdComParameter
    {
        enum class ValidityBehavior
        {
            ZERO,
            KEEP
        };

        bool             marshall{true};
        uint16_t         port{};
        uint8_t          qos{};
        uint8_t          ttl{};
        uint32_t         timeoutUs{};
        ValidityBehavior validityBehavior{ValidityBehavior::ZERO};
        bool             callbackEnabled{false};
    };

    struct MdComParameter
    {
        enum class Protocol
        {
            UDP,
            TCP
        };

        uint16_t udpPort{};
        uint16_t tcpPort{};
        uint32_t confirmTimeoutUs{};
        uint32_t connectTimeoutUs{};
        uint32_t replyTimeoutUs{};
        bool     marshall{true};
        Protocol protocol{Protocol::UDP};
        uint8_t  qos{};
        uint8_t  ttl{};
        uint8_t  retries{};
        uint32_t numSessions{};
    };

    struct SdtParameter
    {
        uint16_t smi1{};
        uint16_t smi2{};
        uint8_t  udv{};
        uint32_t rxPeriodMs{};
        uint32_t txPeriodMs{};
        uint32_t nRxsafe{};
        uint32_t nGrard{};
        uint32_t cmThr{};
    };

    struct DestinationConfig
    {
        uint32_t                    id{};
        std::string                 uri;
        std::string                 name;
        std::optional<SdtParameter> sdt;
    };

    struct PdParameter
    {
        uint32_t                         cycleUs{};
        bool                             marshall{true};
        uint32_t                         timeoutUs{};
        PdComParameter::ValidityBehavior validityBehavior{PdComParameter::ValidityBehavior::KEEP};
        uint8_t                          redundant{};
        bool                             callback{false};
        uint32_t                         offsetAddress{};
    };

    struct TelegramConfig
    {
        std::string                    name;
        uint32_t                       comId{};
        uint32_t                       dataSetId{};
        uint32_t                       comParameterId{};
        std::optional<PdParameter>     pdParam;
        std::vector<DestinationConfig> destinations;
        // TODO: MD-specific parameters later
    };

    struct DataElementConfig
    {
        std::string name;
        uint32_t    type{};
        uint32_t    arraySize{1};
        std::optional<uint32_t> nestedDataSetId;
    };

    struct DataSetConfig
    {
        std::string                    name;
        uint32_t                       id{};
        std::vector<DataElementConfig> elements;
    };

    struct BusInterfaceConfig
    {
        uint32_t                    networkId{};
        std::string                 name;
        std::optional<std::string>  hostIp;
        TrdpProcessConfig           trdpProcess;
        PdComParameter              pdCom;
        MdComParameter              mdCom;
        std::vector<TelegramConfig> telegrams;
    };

    struct MappedTelegramConfig
    {
        uint32_t    comId{};
        std::string name;
    };

    struct MappedBusInterfaceConfig
    {
        std::string                       name;
        std::string                       hostIp;
        std::string                       leaderIp;
        std::vector<MappedTelegramConfig> mappedTelegrams;
    };

    struct MappedDeviceConfig
    {
        std::string                           hostName;
        std::string                           leaderName;
        std::vector<MappedBusInterfaceConfig> interfaces;
    };

    struct DeviceConfig
    {
        std::string                     hostName;
        std::string                     leaderName;
        std::string                     type;
        MemoryConfig                    memory;
        std::optional<DebugConfig>      debug;
        std::optional<PcapConfig>       pcap;
        std::vector<ComParameter>       comParameters;
        std::vector<BusInterfaceConfig> interfaces;
        std::vector<MappedDeviceConfig> mappedDevices;
        std::vector<DataSetConfig>      dataSets;
    };

    // --------- ConfigManager class ----------

    class ConfigManager
    {
      public:
        DeviceConfig loadDeviceConfigFromXml(const std::string& path, bool validateSchema = true);
        void         validateDeviceConfig(const DeviceConfig& cfg);

        std::vector<SchemaIssue> validateXmlSchema(const std::string& path);

        std::vector<data::DataSetDef> buildDataSetDefs(const DeviceConfig& cfg);
    };

} // namespace config
