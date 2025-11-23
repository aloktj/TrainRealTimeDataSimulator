#pragma once

#include <cstdint>

using TRDP_APP_SESSION_T = void*;
using TRDP_PUB_T         = void*;
using TRDP_SUB_T         = void*;

struct TRDP_UUID_T
{
    uint8_t value[16]{};
};

struct TRDP_PD_INFO_T
{
    uint32_t comId{0};
};

struct TRDP_MD_INFO_T
{
    TRDP_UUID_T       sessionId{};
    uint32_t          comId{0};
    uint32_t          resultCode{0};
    uint32_t          msgType{0};
    TRDP_MD_PROTOCOL  protocol{TRDP_MD_UDP};
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

using TRDP_URI_USER_T = TRDP_UUID_T; // Placeholder type for stub

struct TRDP_SEND_PARAM_T
{
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

inline TRDP_ERR_T tlm_request(TRDP_APP_SESSION_T, void* refCon, void* cb, TRDP_UUID_T* sessionId, uint32_t comId,
                              uint32_t etbTopo, uint32_t opTrnTopo, uint32_t srcIp, uint32_t destIp, uint32_t flags,
                              uint32_t numReplies, uint32_t replyTimeout, const TRDP_SEND_PARAM_T*, uint8_t* payload,
                              uint32_t size, TRDP_URI_USER_T, TRDP_URI_USER_T)
{
    (void) refCon;
    (void) cb;
    (void) comId;
    (void) etbTopo;
    (void) opTrnTopo;
    (void) srcIp;
    (void) destIp;
    (void) flags;
    (void) numReplies;
    (void) replyTimeout;
    (void) payload;
    (void) size;
    if (sessionId)
        sessionId->value[0] = 1;
    return TRDP_NO_ERR;
}
inline TRDP_ERR_T tlm_reply(TRDP_APP_SESSION_T, const TRDP_UUID_T*, uint32_t, uint32_t, const TRDP_SEND_PARAM_T*,
                            uint8_t*, uint32_t, const char*)
{
    return TRDP_NO_ERR;
}
