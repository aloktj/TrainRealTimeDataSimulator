#include "backend_api.hpp"

#include <nlohmann/json.hpp>

namespace api {

BackendApi::BackendApi(trdp_sim::EngineContext& ctx,
                       engine::pd::PdEngine& pd,
                       engine::md::MdEngine& md,
                       diag::DiagnosticManager& diag)
    : m_ctx(ctx)
    , m_pd(pd)
    , m_md(md)
    , m_diag(diag)
{
}

nlohmann::json BackendApi::getPdStatus() const
{
    nlohmann::json j;
    // TODO: iterate m_ctx.pdTelegrams and build JSON
    return j;
}

nlohmann::json BackendApi::getDataSetValues(uint32_t dataSetId) const
{
    nlohmann::json j;
    auto it = m_ctx.dataSetInstances.find(dataSetId);
    if (it == m_ctx.dataSetInstances.end())
        return j;

    auto* inst = it->second.get();
    std::lock_guard<std::mutex> lock(inst->mtx);

    // TODO: convert inst->values to JSON using def->elements metadata
    return j;
}

void BackendApi::setDataSetValue(uint32_t dataSetId, std::size_t elementIdx,
                                 const std::vector<uint8_t>& value)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    if (elementIdx >= inst->values.size())
        return;

    inst->values[elementIdx].raw = value;
    inst->values[elementIdx].defined = true;
}

void BackendApi::clearDataSetValue(uint32_t dataSetId, std::size_t elementIdx)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    if (elementIdx >= inst->values.size())
        return;

    inst->values[elementIdx].raw.clear();
    inst->values[elementIdx].defined = false;
}

void BackendApi::clearAllDataSetValues(uint32_t dataSetId)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> lock(inst->mtx);
    for (auto& cell : inst->values) {
        cell.raw.clear();
        cell.defined = false;
    }
}

void BackendApi::lockDataSet(uint32_t dataSetId, bool lock)
{
    auto* inst = m_pd.getDataSetInstance(dataSetId);
    if (!inst)
        return;
    std::lock_guard<std::mutex> guard(inst->mtx);
    inst->locked = lock;
}

uint32_t BackendApi::createMdRequest(uint32_t comId)
{
    return m_md.createRequestSession(comId);
}

void BackendApi::sendMdRequest(uint32_t sessionId)
{
    m_md.sendRequest(sessionId);
}

nlohmann::json BackendApi::getMdSessionStatus(uint32_t sessionId) const
{
    nlohmann::json j;
    auto opt = m_md.getSession(sessionId);
    if (!opt)
        return j;

    auto* sess = *opt;
    std::lock_guard<std::mutex> lock(sess->mtx);
    // TODO: fill JSON with state, retryCount, timestamps, etc.
    return j;
}

void BackendApi::reloadConfiguration(const std::string& xmlPath)
{
    (void)xmlPath;
    // TODO: use ConfigManager to reload XML and re-init EngineContext
}

nlohmann::json BackendApi::getConfigSummary() const
{
    nlohmann::json j;
    // TODO: add hostName, number of interfaces, datasets, pd/md telegram counts
    return j;
}

nlohmann::json BackendApi::getRecentEvents(std::size_t maxEvents) const
{
    nlohmann::json j = nlohmann::json::array();
    auto events = m_diag.fetchRecent(maxEvents);
    for (const auto& ev : events) {
        nlohmann::json item;
        item["component"] = ev.component;
        item["message"] = ev.message;
        // TODO: add timestamp and severity as strings
        j.push_back(std::move(item));
    }
    return j;
}

} // namespace api
