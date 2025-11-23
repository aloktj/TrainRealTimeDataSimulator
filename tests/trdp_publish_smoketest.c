#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if __has_include(<trdp_if_light.h>)
#include <trdp_if_light.h>
#elif __has_include(<trdp_if.h>)
#include <trdp_if.h>
#elif __has_include(<tau_api.h>)
#include <tau_api.h>
#else
#error "No TRDP header found. Ensure TRDP_USE_STUBS is OFF and TRDP headers are available."
#endif

static TRDP_IP_ADDR_T parse_ip(const char *ip)
{
    if (ip == NULL)
    {
        return 0;
    }

    return inet_addr(ip);
}

static void fill_pd_config(TRDP_PD_CONFIG_T *pd_config, UINT16 port)
{
    memset(pd_config, 0, sizeof(*pd_config));
    pd_config->sendParam.qos    = 0;
    pd_config->sendParam.ttl    = 64;
    pd_config->sendParam.retries = 0;
    pd_config->sendParam.tsn    = FALSE;
    pd_config->sendParam.vlan   = 0;
    pd_config->flags            = TRDP_FLAGS_DEFAULT;
    pd_config->timeout          = 0;
    pd_config->toBehavior       = TRDP_TO_KEEP_LAST_VALUE;
    pd_config->port             = port;
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Usage: %s <src_ip> <dst_ip> <dst_port> [com_id]\n", argv[0]);
        return 1;
    }

    const TRDP_IP_ADDR_T src_ip  = parse_ip(argv[1]);
    const TRDP_IP_ADDR_T dest_ip = parse_ip(argv[2]);
    const UINT16         dest_port = (UINT16)strtoul(argv[3], NULL, 10);
    const UINT32         com_id    = (argc > 4) ? (UINT32)strtoul(argv[4], NULL, 10) : 1000U;

    if (src_ip == 0 || dest_ip == 0 || dest_port == 0)
    {
        printf("Invalid parameters provided.\n");
        return 1;
    }

    static uint8_t        mem_pool[1024 * 1024];
    TRDP_MEM_CONFIG_T     mem_config = {.p = mem_pool, .size = sizeof(mem_pool), .prealloc = {0}};
    TRDP_PROCESS_CONFIG_T process_config;
    TRDP_PD_CONFIG_T      pd_config;
    TRDP_SEND_PARAM_T     send_param;
    TRDP_APP_SESSION_T    app_handle = NULL;
    TRDP_ERR_T            err;
    uint8_t               payload[8] = {'T', 'R', 'D', 'P', 'T', 'E', 'S', 'T'};

    memset(&process_config, 0, sizeof(process_config));
    memset(&pd_config, 0, sizeof(pd_config));
    memset(&send_param, 0, sizeof(send_param));

    fill_pd_config(&pd_config, dest_port);
    send_param = pd_config.sendParam;

    printf("TRDP smoketest: COMID=%u %s -> %s:%u\n", com_id, argv[1], argv[2], dest_port);

    err = tlc_init(NULL, NULL, &mem_config);
    if (err != TRDP_NO_ERR)
    {
        printf("tlc_init failed: %d\n", err);
        return 2;
    }

    err = tlc_openSession(&app_handle, src_ip, 0, NULL, &pd_config, NULL, &process_config);
    if (err != TRDP_NO_ERR)
    {
        printf("tlc_openSession failed: %d\n", err);
        tlc_terminate();
        return 2;
    }

    err = tlp_publish(app_handle, NULL, NULL, NULL, 0, com_id, 0, 0, src_ip, dest_ip, dest_port, 0,
                      pd_config.flags, &send_param, payload, sizeof(payload));

    printf("tlp_publish returned: %d\n", err);

    tlc_closeSession(app_handle);
    tlc_terminate();

    return err == TRDP_NO_ERR ? 0 : 2;
}
