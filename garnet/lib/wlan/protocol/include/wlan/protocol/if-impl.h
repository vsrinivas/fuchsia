// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_IF_IMPL_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_IF_IMPL_H_

#include <ddk/protocol/ethernet.h>
#include <net/ethernet.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

enum {
    WLAN_SCAN_TYPE_ACTIVE = 1,
    WLAN_SCAN_TYPE_PASSIVE = 2,
};

// IEEE Std 802.11-2016, 9.4.2.2
#define WLAN_SCAN_MAX_SSIDS 32

typedef struct wlanif_ssid {
    uint8_t len;
    uint8_t data[WLAN_MAX_SSID_LEN];
} wlanif_ssid_t;

typedef struct wlanif_scan_req {
    uint64_t txn_id;
    uint8_t bss_type;  // WLAN_BSS_TYPE_*
    uint8_t bssid[ETH_ALEN];
    wlanif_ssid_t ssid;
    uint8_t scan_type;  // WLAN_SCAN_TYPE_*
    uint32_t probe_delay;
    size_t num_channels;
    uint8_t channel_list[WLAN_CHANNELS_MAX_LEN];
    uint32_t min_channel_time;
    uint32_t max_channel_time;
    size_t num_ssids;
    wlanif_ssid_t ssid_list[WLAN_SCAN_MAX_SSIDS];
} wlanif_scan_req_t;

// IEEE Std 802.11-2016, 9.4.2.25.1
#define WLAN_RSNE_MAX_LEN 255

typedef struct wlanif_bss_description {
    uint8_t bssid[ETH_ALEN];
    wlanif_ssid_t ssid;
    uint8_t bss_type;  // WLAN_BSS_TYPE_*
    uint32_t beacon_period;
    uint32_t dtim_period;
    uint64_t timestamp;
    uint64_t local_time;
    uint16_t cap;
    // Concatenation of SuppRates and ExtSuppRates - MSB indicates basic rate
    // IEEE Std 802.11-2016, 9.4.2.3 & 9.4.2.13
    uint8_t rates[WLAN_MAC_MAX_RATES];
    uint16_t num_rates;

    size_t rsne_len;
    uint8_t rsne[WLAN_RSNE_MAX_LEN];
    wlan_channel_t chan;
    int8_t rssi_dbm;
    int16_t rcpi_dbmh;
    int16_t rsni_dbh;
} wlanif_bss_description_t;

#define WLAN_MAX_OP_RATES 12

typedef struct wlanif_join_req {
    wlanif_bss_description_t selected_bss;
    uint32_t join_failure_timeout;
    uint32_t nav_sync_delay;
    size_t num_op_rates;
    uint16_t op_rates[WLAN_MAX_OP_RATES];
} wlanif_join_req_t;

enum {
    WLAN_AUTH_TYPE_OPEN_SYSTEM = 1,
    WLAN_AUTH_TYPE_SHARED_KEY = 2,
    WLAN_AUTH_TYPE_FAST_BSS_TRANSITION = 3,
    WLAN_AUTH_TYPE_SAE = 4,
};

typedef struct wlanif_auth_req {
    uint8_t peer_sta_address[ETH_ALEN];
    uint8_t auth_type;  // WLAN_AUTH_TYPE_*
    uint32_t auth_failure_timeout;
} wlanif_auth_req_t;

typedef struct wlanif_auth_ind {
    uint8_t peer_sta_address[ETH_ALEN];
    uint8_t auth_type;  // WLAN_AUTH_TYPE_*
} wlanif_auth_ind_t;

// Deauthentication reasons: IEEE Std 802.11-2016, 9.4.1.7 (Table 9-45)
enum {
    // 0 Reserved
    WLAN_DEAUTH_REASON_UNSPECIFIED = 1,
    WLAN_DEAUTH_REASON_INVALID_AUTHENTICATION = 2,
    WLAN_DEAUTH_REASON_LEAVING_NETWORK_DEAUTH = 3,
    WLAN_DEAUTH_REASON_INACTIVITY = 4,
    WLAN_DEAUTH_REASON_NO_MORE_STAS = 5,
    WLAN_DEAUTH_REASON_INVALID_CLASS2_FRAME = 6,
    WLAN_DEAUTH_REASON_INVALID_CLASS3_FRAME = 7,
    WLAN_DEAUTH_REASON_LEAVING_NETWORK_DISASSOC = 8,
    WLAN_DEAUTH_REASON_NOT_AUTHENTICATED = 9,
    WLAN_DEAUTH_REASON_UNACCEPTABLE_POWER_CA = 10,
    WLAN_DEAUTH_REASON_UNACCEPTABLE_SUPPORTED_CHANNELS = 11,
    WLAN_DEAUTH_REASON_BSS_TRANSITION_DISASSOC = 12,
    WLAN_DEAUTH_REASON_INVALID_ELEMENT = 13,
    WLAN_DEAUTH_REASON_MIC_FAILURE = 14,
    WLAN_DEAUTH_REASON_FOURWAY_HANDSHAKE_TIMEOUT = 15,
    WLAN_DEAUTH_REASON_GK_HANDSHAKE_TIMEOUT = 16,
    WLAN_DEAUTH_REASON_HANDSHAKE_ELEMENT_MISMATCH = 17,
    WLAN_DEAUTH_REASON_INVALID_GROUP_CIPHER = 18,
    WLAN_DEAUTH_REASON_INVALID_PAIRWISE_CIPHER = 19,
    WLAN_DEAUTH_REASON_INVALID_AKMP = 20,
    WLAN_DEAUTH_REASON_UNSUPPORTED_RSNE_VERSION = 21,
    WLAN_DEAUTH_REASON_INVALID_RSNE_CAPABILITIES = 22,
    WLAN_DEAUTH_REASON_IEEE802_1_X_AUTH_FAILED = 23,
    WLAN_DEAUTH_REASON_CIPHER_OUT_OF_POLICY = 24,
    WLAN_DEAUTH_REASON_TDLS_PEER_UNREACHABLE = 25,
    WLAN_DEAUTH_REASON_TDLS_UNSPECIFIED = 26,
    WLAN_DEAUTH_REASON_SSP_REQUESTED_DISASSOC = 27,
    WLAN_DEAUTH_REASON_NO_SSP_ROAMING_AGREEMENT = 28,
    WLAN_DEAUTH_REASON_BAD_CIPHER_OR_AKM = 29,
    WLAN_DEAUTH_REASON_NOT_AUTHORIZED_THIS_LOCATION = 30,
    WLAN_DEAUTH_REASON_SERVICE_CHANGE_PRECLUDES_TS = 31,
    WLAN_DEAUTH_REASON_UNSPECIFIED_QOS = 32,
    WLAN_DEAUTH_REASON_NOT_ENOUGH_BANDWIDTH = 33,
    WLAN_DEAUTH_REASON_MISSING_ACKS = 34,
    WLAN_DEAUTH_REASON_EXCEEDED_TXOP = 35,
    WLAN_DEAUTH_REASON_STA_LEAVING = 36,
    // Values 37 and 38 are overloaded but should be clear from context.
    WLAN_DEAUTH_REASON_END_TS_BA_DLS = 37,
    WLAN_DEAUTH_REASON_UNKNOWN_TS_BA = 38,
    WLAN_DEAUTH_REASON_TIMEOUT = 39,
    // 40-44 Reserved
    WLAN_DEAUTH_REASON_PEERKEY_MISMATCH = 45,
    WLAN_DEAUTH_REASON_PEER_INITIATED = 46,
    WLAN_DEAUTH_REASON_AP_INITIATED = 47,
    WLAN_DEAUTH_REASON_INVALID_FT_ACTION_FRAME_COUNT = 48,
    WLAN_DEAUTH_REASON_INVALID_PMKID = 49,
    WLAN_DEAUTH_REASON_INVALID_MDE = 50,
    WLAN_DEAUTH_REASON_INVALID_FTE = 51,
    WLAN_DEAUTH_REASON_MESH_PEERING_CANCELED = 52,
    WLAN_DEAUTH_REASON_MESH_MAX_PEERS = 53,
    WLAN_DEAUTH_REASON_MESH_CONFIGURATION_POLICY_VIOLATION = 54,
    WLAN_DEAUTH_REASON_MESH_CLOSE_RCVD = 55,
    WLAN_DEAUTH_REASON_MESH_MAX_RETRIES = 56,
    WLAN_DEAUTH_REASON_MESH_CONFIRM_TIMEOUT = 57,
    WLAN_DEAUTH_REASON_MESH_INVALID_GTK = 58,
    WLAN_DEAUTH_REASON_MESH_INCONSISTENT_PARAMETERS = 59,
    WLAN_DEAUTH_REASON_MESH_INVALID_SECURITY_CAPABILITY = 60,
    WLAN_DEAUTH_REASON_MESH_PATH_ERROR_NO_PROXY_INFORMATION = 61,
    WLAN_DEAUTH_REASON_MESH_PATH_ERROR_NO_FORWARDING_INFORMATION = 62,
    WLAN_DEAUTH_REASON_MESH_PATH_ERROR_DESTINATION_UNREACHABLE = 63,
    WLAN_DEAUTH_REASON_MAC_ADDRESS_ALREADY_EXISTS_IN_MBSS = 64,
    WLAN_DEAUTH_REASON_MESH_CHANNEL_SWITCH_REGULATORY_REQUIREMENTS = 65,
    WLAN_DEAUTH_REASON_MESH_CHANNEL_SWITCH_UNSPECIFIED = 66,
    // 67 - 65535 Reserved
};

typedef struct wlanif_deauth_req {
    uint8_t peer_sta_address[ETH_ALEN];
    uint16_t reason_code;  // WLAN_DEAUTH_REASON_*
} wlanif_deauth_req_t;

typedef struct wlanif_assoc_req {
    uint8_t peer_sta_address[ETH_ALEN];
    size_t rsne_len;
    uint8_t rsne[WLAN_RSNE_MAX_LEN];
} wlanif_assoc_req_t;

typedef struct wlanif_assoc_ind {
    uint8_t peer_sta_address[ETH_ALEN];
    uint16_t listen_interval;
    wlanif_ssid_t ssid;
    size_t rsne_len;
    uint8_t rsne[WLAN_RSNE_MAX_LEN];
} wlanif_assoc_ind_t;

typedef struct wlanif_disassoc_req {
    uint8_t peer_sta_address[ETH_ALEN];
    uint16_t reason_code;
} wlanif_disassoc_req_t;

typedef struct wlanif_reset_req {
    uint8_t sta_address[ETH_ALEN];
    bool set_default_mib;
} wlanif_reset_req_t;

typedef struct wlanif_start_req {
    wlanif_ssid_t ssid;
    uint8_t bss_type;  // WLAN_BSS_TYPE_*
    uint32_t beacon_period;
    uint32_t dtim_period;
    uint8_t channel;
    size_t rsne_len;
    uint8_t rsne[WLAN_RSNE_MAX_LEN];
} wlanif_start_req_t;

typedef struct wlanif_stop_req {
    wlanif_ssid_t ssid;
} wlanif_stop_req_t;

typedef struct set_key_descriptor {
    uint8_t* key;
    uint16_t length;
    uint16_t key_id;
    uint8_t key_type;  // WLAN_KEY_TYPE_*
    uint8_t address[ETH_ALEN];
    uint8_t rsc[8];
    uint8_t cipher_suite_oui[3];
    uint8_t cipher_suite_type;
} set_key_descriptor_t;

#define WLAN_MAX_KEYLIST_SIZE 4

typedef struct wlanif_set_keys_req {
    size_t num_keys;
    set_key_descriptor_t keylist[WLAN_MAX_KEYLIST_SIZE];
} wlanif_set_keys_req_t;

typedef struct delete_key_descriptor {
    uint16_t key_id;
    uint8_t key_type;  // WLAN_KEY_TYPE_*
    uint8_t address[ETH_ALEN];
} delete_key_descriptor_t;

typedef struct wlanif_del_keys_req {
    size_t num_keys;
    delete_key_descriptor_t keylist[WLAN_MAX_KEYLIST_SIZE];
} wlanif_del_keys_req_t;

typedef struct wlanif_eapol_req {
    uint8_t src_addr[ETH_ALEN];
    uint8_t dst_addr[ETH_ALEN];
    size_t data_len;
    uint8_t* data;
} wlanif_eapol_req_t;

// Bits used to request management frame subtypes to be captured. Also used by driver to indicate
// which management frame subtypes are supported for capture.
//
// These values are set at `1 << MgmtFrameSubtypeValue`
// See IEEE Std 802.11-2016, 9.2.4.1.3, for value of each management frame subtype
enum {
    WLAN_MGMT_CAPTURE_FLAG_ASSOC_REQ = 1 << 0,
    WLAN_MGMT_CAPTURE_FLAG_ASSOC_RESP = 1 << 1,
    WLAN_MGMT_CAPTURE_FLAG_REASSOC_REQ = 1 << 2,
    WLAN_MGMT_CAPTURE_FLAG_REASSOC_RESP = 1 << 3,
    WLAN_MGMT_CAPTURE_FLAG_PROBE_REQ = 1 << 4,
    WLAN_MGMT_CAPTURE_FLAG_PROBE_RESP = 1 << 5,
    WLAN_MGMT_CAPTURE_FLAG_TIMING_AD = 1 << 6,

    WLAN_MGMT_CAPTURE_FLAG_BEACON = 1 << 8,
    WLAN_MGMT_CAPTURE_FLAG_ATIM = 1 << 9,
    WLAN_MGMT_CAPTURE_FLAG_DISASSOC = 1 << 10,
    WLAN_MGMT_CAPTURE_FLAG_AUTH = 1 << 11,
    WLAN_MGMT_CAPTURE_FLAG_DEAUTH = 1 << 12,
    WLAN_MGMT_CAPTURE_FLAG_ACTION = 1 << 13,
    WLAN_MGMT_CAPTURE_FLAG_ACTION_NO_ACK = 1 << 14,
};

typedef struct wlanif_start_capture_frames_req {
    uint32_t mgmt_frame_flags;
} wlanif_start_capture_frames_req_t;

typedef struct wlanif_start_capture_frames_resp {
    int32_t status;
    uint32_t supported_mgmt_frames;
} wlanif_start_capture_frames_resp_t;

typedef struct wlanif_scan_result {
    uint64_t txn_id;
    wlanif_bss_description_t bss;
} wlanif_scan_result_t;

enum {
    WLAN_SCAN_RESULT_SUCCESS = 0,
    WLAN_SCAN_RESULT_NOT_SUPPORTED = 1,
    WLAN_SCAN_RESULT_INVALID_ARGS = 2,
    WLAN_SCAN_RESULT_INTERNAL_ERROR = 3,
};

typedef struct wlanif_scan_end {
    uint64_t txn_id;
    uint8_t code;  // WLAN_SCAN_RESULT_*
} wlanif_scan_end_t;

enum {
    WLAN_JOIN_RESULT_SUCCESS = 0,
    WLAN_JOIN_RESULT_FAILURE_TIMEOUT = 1,
};

typedef struct wlanif_join_confirm {
    uint8_t result_code;  // WLAN_JOIN_RESULT_*
} wlanif_join_confirm_t;

enum {
    WLAN_AUTH_RESULT_SUCCESS = 0,
    WLAN_AUTH_RESULT_REFUSED = 1,
    WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED = 2,
    WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED = 3,
    WLAN_AUTH_RESULT_REJECTED = 4,
    WLAN_AUTH_RESULT_FAILURE_TIMEOUT = 5,
};

typedef struct wlanif_auth_confirm {
    uint8_t peer_sta_address[ETH_ALEN];
    uint8_t auth_type;    // WLAN_AUTH_TYPE_*
    uint8_t result_code;  // WLAN_AUTH_RESULT_*
} wlanif_auth_confirm_t;

typedef struct wlanif_auth_resp {
    uint8_t peer_sta_address[ETH_ALEN];
    uint8_t result_code;  // WLAN_AUTH_RESULT_*
} wlanif_auth_resp_t;

typedef struct wlanif_deauth_confirm {
    uint8_t peer_sta_address[ETH_ALEN];
} wlanif_deauth_confirm_t;

typedef struct wlanif_deauth_indication {
    uint8_t peer_sta_address[ETH_ALEN];
    uint16_t reason_code;  // WLAN_DEAUTH_REASON_*
} wlanif_deauth_indication_t;

enum {
    WLAN_ASSOC_RESULT_SUCCESS = 0,
    WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED = 1,
    WLAN_ASSOC_RESULT_REFUSED_NOT_AUTHENTICATED = 2,
    WLAN_ASSOC_RESULT_REFUSED_CAPABILITIES_MISMATCH = 3,
    WLAN_ASSOC_RESULT_REFUSED_EXTERNAL_REASON = 4,
    WLAN_ASSOC_RESULT_REFUSED_AP_OUT_OF_MEMORY = 5,
    WLAN_ASSOC_RESULT_REFUSED_BASIC_RATES_MISMATCH = 6,
    WLAN_ASSOC_RESULT_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED = 7,
    WLAN_ASSOC_RESULT_REFUSED_TEMPORARILY = 8,
};

typedef struct wlanif_assoc_confirm {
    uint8_t result_code;  // WLAN_ASSOC_RESULT_*
    uint16_t association_id;
} wlanif_assoc_confirm_t;

typedef struct wlanif_assoc_resp {
    uint8_t peer_sta_address[ETH_ALEN];
    uint8_t result_code;  // WLAN_ASSOC_RESULT_*
    uint16_t association_id;
} wlanif_assoc_resp_t;

typedef struct wlanif_disassoc_confirm {
    int32_t status;
} wlanif_disassoc_confirm_t;

typedef struct wlanif_disassoc_indication {
    uint8_t peer_sta_address[ETH_ALEN];
    uint16_t reason_code;  // WLAN_DEAUTH_REASON_*
} wlanif_disassoc_indication_t;

enum {
    WLAN_START_RESULT_SUCCESS = 0,
    WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED = 1,
    WLAN_START_RESULT_RESET_REQUIRED_BEFORE_START = 2,
    WLAN_START_RESULT_NOT_SUPPORTED = 3,
};

typedef struct wlanif_start_confirm {
    uint8_t result_code;  // WLAN_START_RESULT_*
} wlanif_start_confirm_t;

enum {
    WLAN_STOP_RESULT_SUCCESS = 0,
    WLAN_STOP_RESULT_BSS_ALREADY_STOPPED = 1,
    WLAN_STOP_RESULT_INTERNAL_ERROR = 2,
};

typedef struct wlanif_stop_confirm {
    uint8_t result_code;  // WLAN_STOP_RESULT_*
} wlanif_stop_confirm_t;

enum {
    WLAN_EAPOL_RESULT_SUCCESS = 0,
    WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE = 1,
};

typedef struct wlanif_eapol_confirm {
    uint8_t result_code;  // WLAN_EAPOL_RESULT_*
} wlanif_eapol_confirm_t;

typedef struct wlanif_signal_report_indication {
    int8_t rssi_dbm;
} wlanif_signal_report_indication_t;

typedef struct wlanif_eapol_indication {
    uint8_t src_addr[ETH_ALEN];
    uint8_t dst_addr[ETH_ALEN];
    size_t data_len;
    uint8_t* data;
} wlanif_eapol_indication_t;

typedef struct wlanif_band_capabilities {
    uint8_t band_id; // Values from enum Band (WLAN_BAND_*)
    size_t num_basic_rates;
    uint16_t basic_rates[WLAN_BASIC_RATES_MAX_LEN];
    uint16_t base_frequency;
    size_t num_channels;
    uint8_t channels[WLAN_CHANNELS_MAX_LEN];
    bool ht_supported;
    wlan_ht_caps_t ht_caps;
    bool vht_supported;
    wlan_vht_caps_t vht_caps;
} wlanif_band_capabilities_t;

enum {
    WLANIF_FEATURE_DMA = 1UL << 0,    // Supports DMA buffer transfer protocol
    WLANIF_FEATURE_SYNTH = 1UL << 1,  // Synthetic (i.e., non-physical) device
};

typedef struct wlanif_query_info {
    uint8_t mac_addr[ETH_ALEN];
    uint8_t role;       // WLAN_MAC_ROLE_*
    uint32_t features;  // WLANIF_FEATURE_*
    size_t num_bands;
    wlanif_band_capabilities_t bands[WLAN_MAX_BANDS];
    uint32_t driver_features;  // WLAN_DRIVER_FEATURE_*
} wlanif_query_info_t;

typedef struct wlanif_counter {
    uint64_t count;
    char* name;
} wlanif_counter_t;

typedef struct wlanif_packet_count {
    wlanif_counter_t in;
    wlanif_counter_t out;
    wlanif_counter_t drop;
    wlanif_counter_t in_bytes;
    wlanif_counter_t out_bytes;
    wlanif_counter_t drop_bytes;
} wlanif_packet_counter_t;

typedef struct wlanif_dispatcher_stats {
    wlanif_packet_counter_t any_packet;
    wlanif_packet_counter_t mgmt_frame;
    wlanif_packet_counter_t ctrl_frame;
    wlanif_packet_counter_t data_frame;
} wlanif_dispatcher_stats_t;

typedef struct wlanif_rssi_stats {
    size_t hist_len;
    uint64_t* hist;
} wlanif_rssi_stats_t;

typedef struct wlanif_client_mlme_stats {
    wlanif_packet_counter_t svc_msg;
    wlanif_packet_counter_t data_frame;
    wlanif_packet_counter_t mgmt_frame;
    wlanif_packet_counter_t tx_frame;
    wlanif_packet_counter_t rx_frame;
    wlanif_rssi_stats_t assoc_data_rssi;
    wlanif_rssi_stats_t beacon_rssi;
} wlanif_client_mlme_stats_t;

typedef struct wlanif_ap_mlme_stats {
    wlanif_packet_counter_t not_used;
} wlanif_ap_mlme_stats_t;

enum {
    WLANIF_MLME_STATS_TYPE_CLIENT,
    WLANIF_MLME_STATS_TYPE_AP,
};

typedef union wlanif_mlme_stats {
    uint8_t tag;  // WLANIF_MLME_STATS_TYPE_*
    union {
        wlanif_client_mlme_stats_t client_mlme_stats;
        wlanif_ap_mlme_stats_t ap_mlme_stats;
    };
} wlanif_mlme_stats_t;

typedef struct wlanif_stats {
    wlanif_dispatcher_stats_t dispatcher_stats;
    wlanif_mlme_stats_t* mlme_stats;
} wlanif_stats_t;

typedef struct wlanif_stats_query_response {
    wlanif_stats_t stats;
} wlanif_stats_query_response_t;

typedef struct wlanif_captured_frame_result {
    size_t data_len;
    uint8_t* data;
} wlanif_captured_frame_result_t;

typedef struct wlanif_impl_ifc {
    // MLME operations
    void (*on_scan_result)(void* cookie, wlanif_scan_result_t* result);
    void (*on_scan_end)(void* cookie, wlanif_scan_end_t* end);
    void (*join_conf)(void* cookie, wlanif_join_confirm_t* resp);
    void (*auth_conf)(void* cookie, wlanif_auth_confirm_t* resp);
    void (*auth_ind)(void* cookie, wlanif_auth_ind_t* resp);
    void (*deauth_conf)(void* cookie, wlanif_deauth_confirm_t* resp);
    void (*deauth_ind)(void* cookie, wlanif_deauth_indication_t* ind);
    void (*assoc_conf)(void* cookie, wlanif_assoc_confirm_t* resp);
    void (*assoc_ind)(void* cookie, wlanif_assoc_ind_t* resp);
    void (*disassoc_conf)(void* cookie, wlanif_disassoc_confirm_t* resp);
    void (*disassoc_ind)(void* cookie, wlanif_disassoc_indication_t* ind);
    void (*start_conf)(void* cookie, wlanif_start_confirm_t* resp);
    void (*stop_conf)(void* cookie, wlanif_stop_confirm_t* resp);
    void (*eapol_conf)(void* cookie, wlanif_eapol_confirm_t* resp);

    // MLME extensions
    void (*signal_report)(void* cookie, wlanif_signal_report_indication_t* ind);
    void (*eapol_ind)(void* cookie, wlanif_eapol_indication_t* ind);
    void (*stats_query_resp)(void* cookie, wlanif_stats_query_response_t* resp);
    void (*relay_captured_frame)(void* cookie, wlanif_captured_frame_result_t* result);

    // Data operations
    void (*data_recv)(void* cookie, void* data, size_t length, uint32_t flags);
    void (*data_complete_tx)(void* cookie, ethmac_netbuf_t* netbuf, zx_status_t status);
} wlanif_impl_ifc_t;

typedef struct wlanif_impl_protocol_ops {
    // Lifecycle operations
    zx_status_t (*start)(void* ctx, wlanif_impl_ifc_t* ifc, zx_handle_t* out_sme_channel,
                         void* cookie);
    void (*stop)(void* ctx);

    // State operation
    void (*query)(void* ctx, wlanif_query_info_t* info);

    // MLME operations
    void (*start_scan)(void* ctx, wlanif_scan_req_t* req);
    void (*join_req)(void* ctx, wlanif_join_req_t* req);
    void (*auth_req)(void* ctx, wlanif_auth_req_t* req);
    void (*auth_resp)(void* ctx, wlanif_auth_resp_t* resp);
    void (*deauth_req)(void* ctx, wlanif_deauth_req_t* req);
    void (*assoc_req)(void* ctx, wlanif_assoc_req_t* req);
    void (*assoc_resp)(void* ctx, wlanif_assoc_resp_t* resp);
    void (*disassoc_req)(void* ctx, wlanif_disassoc_req_t* req);
    void (*reset_req)(void* ctx, wlanif_reset_req_t* req);
    void (*start_req)(void* ctx, wlanif_start_req_t* req);
    void (*stop_req)(void* ctx, wlanif_stop_req_t* req);
    void (*set_keys_req)(void* ctx, wlanif_set_keys_req_t* req);
    void (*del_keys_req)(void* ctx, wlanif_del_keys_req_t* req);
    void (*eapol_req)(void* ctx, wlanif_eapol_req_t* req);

    // MLME extensions
    void (*stats_query_req)(void* ctx);
    void (*start_capture_frames)(void* ctx, wlanif_start_capture_frames_req_t* req,
                                 wlanif_start_capture_frames_resp_t* resp);
    void (*stop_capture_frames)(void* ctx);

    // Data operations
    zx_status_t (*data_queue_tx)(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf);

} wlanif_impl_protocol_ops_t;

typedef struct wlanif_impl_protocol {
    wlanif_impl_protocol_ops_t* ops;
    void* ctx;
} wlanif_impl_protocol_t;

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_IF_IMPL_H_
