#pragma once

#include <cstdint>

using TRDP_APP_SESSION_T = void*;
using TRDP_PUB_T         = void*;
using TRDP_SUB_T         = void*;
using TRDP_LR_T          = uint32_t;

struct TRDP_PD_INFO_T
{
    uint32_t comId{0};
};

struct TRDP_MD_INFO_T
{
    uint32_t sessionId{0};
};

enum TRDP_ERR_T
{
    TRDP_NO_ERR      = 0,
    TRDP_ERR_GENERIC = 1,
};

enum TRDP_TO_BEHAVIOR
{
    TRDP_TO_KEEP_LAST_VALUE,
    TRDP_TO_ZERO,
};

enum TRDP_MD_PROTOCOL
{
    TRDP_MD_UDP,
    TRDP_MD_TCP,
};

struct TRDP_MEM_CONFIG_T
{
};
struct TRDP_PD_CONFIG_T
{
    uint32_t         timeout{0};
    TRDP_TO_BEHAVIOR toBehavior{TRDP_TO_KEEP_LAST_VALUE};
};
struct TRDP_MD_CONFIG_T
{
    uint32_t confirmTimeout{0};
    uint32_t replyTimeout{0};
};
struct TRDP_PROCESS_CONFIG_T
{
    char* szHostname{nullptr};
};

#ifndef UINT8
using UINT8  = uint8_t;
using UINT32 = uint32_t;
#endif

inline TRDP_ERR_T tlc_init(TRDP_APP_SESSION_T* session, TRDP_MEM_CONFIG_T*, void*, TRDP_PD_CONFIG_T*, TRDP_MD_CONFIG_T*,
                           TRDP_PROCESS_CONFIG_T*)
{
    if (session)
        *session = reinterpret_cast<TRDP_APP_SESSION_T>(0x1);
    return TRDP_NO_ERR;
}

inline void       tlc_terminate(TRDP_APP_SESSION_T) {}
inline TRDP_ERR_T tlc_process(TRDP_APP_SESSION_T, void*, void*)
{
    return TRDP_NO_ERR;
}
inline TRDP_ERR_T tlc_getInterval(TRDP_APP_SESSION_T, void*, void*, int32_t* noDesc)
{
    if (noDesc)
        *noDesc = 0;
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_publish(TRDP_APP_SESSION_T, TRDP_PUB_T*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              TRDP_PD_CONFIG_T*, void*, uint32_t)
{
    return TRDP_NO_ERR;
}
inline TRDP_ERR_T tlp_subscribe(TRDP_APP_SESSION_T, TRDP_SUB_T*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                TRDP_PD_CONFIG_T*, void*, void*)
{
    return TRDP_NO_ERR;
}
inline TRDP_ERR_T tlp_put(TRDP_APP_SESSION_T, TRDP_PUB_T, uint8_t*, uint32_t)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlm_request(TRDP_APP_SESSION_T, TRDP_LR_T* handle, void*, void*, void*, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    if (handle)
        *handle = 1;
    return TRDP_NO_ERR;
}
inline TRDP_ERR_T tlm_reply(TRDP_APP_SESSION_T, TRDP_LR_T, void*, void*, uint32_t, uint32_t, uint8_t*, uint32_t,
                            uint32_t, uint32_t, uint32_t, uint32_t)
{
    return TRDP_NO_ERR;
}
