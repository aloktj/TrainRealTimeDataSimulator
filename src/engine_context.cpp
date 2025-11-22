// src/engine_context.cpp

#include "engine_context.hpp"

#include "md_engine.hpp"
#include "pd_engine.hpp"

namespace trdp_sim
{

    // Currently EngineContext is a plain struct with no behavior.
    // This file is kept for future helper functions or initialization
    // utilities that operate on EngineContext.

    EngineContext::~EngineContext() = default;

    void PdRuntimeDeleter::operator()(engine::pd::PdTelegramRuntime* ptr) const
    {
        delete ptr;
    }

    void MdSessionDeleter::operator()(engine::md::MdSessionRuntime* ptr) const
    {
        delete ptr;
    }

    // Example placeholder function (optional):
    // void initEngineContext(EngineContext& ctx)
    // {
    //     ctx.running = false;
    //     // Add any default initialization here later.
    // }

} // namespace trdp_sim
