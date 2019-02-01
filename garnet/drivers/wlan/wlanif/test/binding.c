// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#define _ALL_SOURCE
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <wlan/protocol/if-impl.h>
#include <zircon/types.h>

wlanif_impl_ifc_t wlanif_ifc = {};
void* wlanif_cookie = NULL;
zx_device_t* global_device;
uint64_t scan_txn_id;

zx_status_t wlanif_start(void* ctx, wlanif_impl_ifc_t* ifc, void* cookie) {
    printf("***** wlanif_start called\n");
    memcpy(&wlanif_ifc, ifc, sizeof(wlanif_ifc));
    wlanif_cookie = cookie;
    return ZX_OK;
}

void wlanif_stop(void* ctx) {}

#define MAX_SSID_LEN 100

wlanif_bss_description_t scan_results[] = {
    {.bssid = {11, 22, 33, 44, 55, 66},
     .ssid.data = {'F', 'a', 'k', 'e', ' ', 'A', 'P', ' ', '1'},
     .ssid.len = 9,
     .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
     .beacon_period = 1,
     .dtim_period = 1,
     .timestamp = 0,
     .local_time = 0,
     .num_rates = 12,
     .rates = {128 + 2, 128 + 4, 128 + 11, 128 + 22, 12, 18, 24, 36, 48, 72, 96, 108},
     .rsne_len = 0,
     .chan = {.primary = 4, .cbw = CBW20, .secondary80 = 0}}};

#define NUM_SCAN_RESULTS countof(scan_results)

int fake_scan_results(void* arg) {
    printf("***** faking scan results!\n");
    for (unsigned int iter = 0; iter < NUM_SCAN_RESULTS; iter++) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
        wlanif_scan_result_t scan_result;
        scan_result.txn_id = scan_txn_id;
        scan_result.bss = scan_results[iter];
        wlanif_ifc.on_scan_result(wlanif_cookie, &scan_result);
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
    wlanif_scan_end_t scan_end = {.txn_id = scan_txn_id, .code = WLAN_SCAN_RESULT_SUCCESS};
    wlanif_ifc.on_scan_end(wlanif_cookie, &scan_end);
    return 0;
}

void wlanif_start_scan(void* ctx, wlanif_scan_req_t* req) {
    printf("***** starting scan (txn_id = %lu)!!!\n", req->txn_id);
    scan_txn_id = req->txn_id;
    thrd_t scan_thrd;
    thrd_create_with_name(&scan_thrd, fake_scan_results, NULL, "wlanif-test-fake-scan");
    return;
}

void wlanif_join_req(void* ctx, wlanif_join_req_t* req) {
    printf("***** join_req\n");
    wlanif_join_confirm_t conf = {.result_code = WLAN_JOIN_RESULT_SUCCESS};
    wlanif_ifc.join_conf(wlanif_cookie, &conf);
}

void wlanif_auth_req(void* ctx, wlanif_auth_req_t* req) {
    printf("***** auth_req\n");
    wlanif_auth_confirm_t conf;
    memcpy(conf.peer_sta_address, req->peer_sta_address, ETH_ALEN);
    conf.auth_type = req->auth_type;
    conf.result_code = WLAN_AUTH_RESULT_SUCCESS;
    wlanif_ifc.auth_conf(wlanif_cookie, &conf);
}

void wlanif_auth_resp(void* ctx, wlanif_auth_resp_t* ind) {
    printf("***** auth_ind\n");
}

void wlanif_deauth_req(void* ctx, wlanif_deauth_req_t* req) {
    printf("***** deauth_req\n");
}

void wlanif_assoc_req(void* ctx, wlanif_assoc_req_t* req) {
    printf("***** assoc_req\n");
    wlanif_assoc_confirm_t conf;
    conf.result_code = WLAN_ASSOC_RESULT_SUCCESS;
    conf.association_id = 0;
    wlanif_ifc.assoc_conf(wlanif_cookie, &conf);
}

void wlanif_assoc_resp(void* ctx, wlanif_assoc_resp_t* ind) {
    printf("***** assoc_ind\n");
}

void wlanif_disassoc_req(void* ctx, wlanif_disassoc_req_t* req) {
    printf("***** disassoc_req\n");
}

void wlanif_reset_req(void* ctx, wlanif_reset_req_t* req) {
    printf("***** reset_req\n");
}

void wlanif_start_req(void* ctx, wlanif_start_req_t* req) {
    printf("***** start_req\n");
}

void wlanif_stop_req(void* ctx, wlanif_stop_req_t* req) {
    printf("***** stop_req\n");
}

void wlanif_set_keys_req(void* ctx, wlanif_set_keys_req_t* req) {
    printf("***** set_keys_req\n");
}

void wlanif_del_keys_req(void* ctx, wlanif_del_keys_req_t* req) {
    printf("***** del_keys_req\n");
}

void wlanif_eapol_req(void* ctx, wlanif_eapol_req_t* req) {
    printf("***** eapol_req\n");
}

void wlanif_query(void* ctx, wlanif_query_info_t* info) {
    printf("***** query\n");
    memset(info, 0, sizeof(*info));

    // mac_addr
    uint8_t mac_addr[ETH_ALEN] = {1, 2, 3, 4, 5, 6};
    memcpy(info->mac_addr, mac_addr, ETH_ALEN);

    // role
    info->role = WLAN_MAC_ROLE_CLIENT;

    // features
    info->features = 0;

    // num_bands
    info->num_bands = 1;

    // basic_rates
    const uint16_t basic_rates[] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
    static_assert(countof(basic_rates) <= WLAN_BASIC_RATES_MAX_LEN, "too many basic rates");
    size_t num_rates = countof(basic_rates);
    info->bands[0].num_basic_rates = num_rates;
    memcpy(info->bands[0].basic_rates, basic_rates, sizeof(basic_rates));

    // base_frequency
    info->bands[0].base_frequency = 2407;

    // channels
    const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    static_assert(countof(channels) <= WLAN_CHANNELS_MAX_LEN, "too many channels");
    size_t num_channels = countof(channels);
    info->bands[0].num_channels = num_channels;
    memcpy(info->bands[0].channels, channels, sizeof(channels));
}

zx_status_t wlanif_data_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    printf("***** data_queue_tx\n");
    return ZX_OK;
}

wlanif_impl_protocol_ops_t wlanif_impl_ops = {
    .start = wlanif_start,
    .stop = wlanif_stop,
    .query = wlanif_query,
    .start_scan = wlanif_start_scan,
    .join_req = wlanif_join_req,
    .auth_req = wlanif_auth_req,
    .auth_resp = wlanif_auth_resp,
    .deauth_req = wlanif_deauth_req,
    .assoc_req = wlanif_assoc_req,
    .assoc_resp = wlanif_assoc_resp,
    .disassoc_req = wlanif_disassoc_req,
    .reset_req = wlanif_reset_req,
    .start_req = wlanif_start_req,
    .stop_req = wlanif_stop_req,
    .set_keys_req = wlanif_set_keys_req,
    .del_keys_req = wlanif_del_keys_req,
    .eapol_req = wlanif_eapol_req,
    .data_queue_tx = wlanif_data_queue_tx,
};

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
};

zx_status_t dev_bind(void* ctx, zx_device_t* device) {
    static bool first = true;

    if (!first) { return ZX_ERR_ALREADY_BOUND; }
    first = false;
    static device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "wlanif-test",
        .ctx = NULL,
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_WLANIF_IMPL,
        .proto_ops = &wlanif_impl_ops,
    };
    return device_add(device, &args, &global_device);
}

zx_status_t dev_init(void** out_ctx) {
    return ZX_OK;
}

void dev_release(void* ctx) {}

static zx_driver_ops_t wlanif_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = dev_init,
    .bind = dev_bind,
    .release = dev_release,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(wlanif-test, wlanif_test_driver_ops, "fuchsia", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(wlanif-test)
// clang-format on
