#pragma once

#include <cstdint>
#include <cstring>
#include <sys/time.h>

using TRDP_APP_SESSION_T = void*;
using TRDP_PUB_T         = void*;
using TRDP_SUB_T         = void*;
using TRDP_FDS_T         = void*;
using TRDP_SOCK_T        = int;
using TRDP_TIME_T        = timeval;

#ifndef UINT8
using UINT8  = uint8_t;
using UINT32 = uint32_t;
#endif

enum TRDP_ERR_T
{
    TRDP_NO_ERR      = 0,
    TRDP_ERR_GENERIC = 1,
};

enum TRDP_TO_BEHAVIOR_T
{
    TRDP_TO_ZERO             = 0,
    TRDP_TO_KEEP_LAST_VALUE  = 1,
};

enum TRDP_MD_PROTOCOL
{
    TRDP_MD_UDP,
    TRDP_MD_TCP,
};

using TRDP_FLAGS_T    = uint32_t;
using TRDP_IP_ADDR_T  = uint32_t;
using TRDP_PRINT_DBG_T = void (*)(void*, int, const char*, const char*, uint16_t, const char*);
using TRDP_PD_CALLBACK_T = void (*)(void*, TRDP_APP_SESSION_T, const struct TRDP_PD_INFO_T*, UINT8*, UINT32);
using TRDP_MD_CALLBACK_T = void (*)(void*, TRDP_APP_SESSION_T, const struct TRDP_MD_INFO_T*, UINT8*, UINT32);

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
    TRDP_UUID_T      sessionId{};
    uint32_t         comId{0};
    uint32_t         resultCode{0};
    uint32_t         msgType{0};
    TRDP_MD_PROTOCOL protocol{TRDP_MD_UDP};
};

struct TRDP_SEND_PARAM_T
{
};

struct TRDP_MEM_CONFIG_T
{
};

struct TRDP_PD_CONFIG_T
{
    uint32_t            timeout{0};
    TRDP_TO_BEHAVIOR_T  toBehavior{TRDP_TO_KEEP_LAST_VALUE};
};

struct TRDP_MD_CONFIG_T
{
    uint32_t confirmTimeout{0};
    uint32_t replyTimeout{0};
};

struct TRDP_PROCESS_CONFIG_T
{
    char* hostName{nullptr};
    char* szHostname{nullptr};
};

using TRDP_URI_USER_T = TRDP_UUID_T; // Placeholder type for stub

inline TRDP_ERR_T tlc_init(TRDP_PRINT_DBG_T, void* refCon, const TRDP_MEM_CONFIG_T*)
{
    (void)refCon;
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlc_openSession(TRDP_APP_SESSION_T* session, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, const void*,
                                  const TRDP_PD_CONFIG_T*, const TRDP_MD_CONFIG_T*, const TRDP_PROCESS_CONFIG_T*)
{
    if (session)
        *session = reinterpret_cast<TRDP_APP_SESSION_T>(0x1);
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlc_closeSession(TRDP_APP_SESSION_T)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlc_terminate()
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlc_process(TRDP_APP_SESSION_T, TRDP_FDS_T*, int32_t*)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlc_getInterval(TRDP_APP_SESSION_T, TRDP_TIME_T* interval, TRDP_FDS_T*, TRDP_SOCK_T* noDesc)
{
    if (interval)
        std::memset(interval, 0, sizeof(TRDP_TIME_T));
    if (noDesc)
        *noDesc = 0;
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_publish(TRDP_APP_SESSION_T, TRDP_PUB_T* handle, void*, TRDP_PD_CALLBACK_T, UINT32, UINT32,
                              UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, UINT32, UINT32, TRDP_FLAGS_T,
                              const TRDP_SEND_PARAM_T*, const UINT8*, UINT32)
{
    if (handle)
        *handle = reinterpret_cast<TRDP_PUB_T>(0x2);
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_subscribe(TRDP_APP_SESSION_T, TRDP_SUB_T* handle, void*, TRDP_PD_CALLBACK_T, UINT32, UINT32,
                                UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_FLAGS_T,
                                const void*, UINT32, TRDP_TO_BEHAVIOR_T)
{
    if (handle)
        *handle = reinterpret_cast<TRDP_SUB_T>(0x3);
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_put(TRDP_APP_SESSION_T, TRDP_PUB_T, const UINT8*, UINT32)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_unpublish(TRDP_APP_SESSION_T, TRDP_PUB_T)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlp_unsubscribe(TRDP_APP_SESSION_T, TRDP_SUB_T)
{
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlm_request(TRDP_APP_SESSION_T, void*, TRDP_MD_CALLBACK_T, TRDP_UUID_T* sessionId, UINT32,
                              UINT32, UINT32, TRDP_IP_ADDR_T, TRDP_IP_ADDR_T, TRDP_FLAGS_T, UINT32, UINT32,
                              const TRDP_SEND_PARAM_T*, const UINT8*, UINT32, const TRDP_URI_USER_T,
                              const TRDP_URI_USER_T)
{
    if (sessionId)
        sessionId->value[0] = 1;
    return TRDP_NO_ERR;
}

inline TRDP_ERR_T tlm_reply(TRDP_APP_SESSION_T, const TRDP_UUID_T*, UINT32, UINT32, const TRDP_SEND_PARAM_T*,
                            const UINT8*, UINT32, const char*)
{
    return TRDP_NO_ERR;
}
