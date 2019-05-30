// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_

#include <stdint.h>

#include <ddk/hw/wlan/ieee80211.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// IEEE Std 802.11-2016, 9.4.2.2
#define WLAN_MAX_SSID_LEN 32

typedef struct wlan_ssid {
    uint8_t len;
    uint8_t ssid[WLAN_MAX_SSID_LEN];
} wlan_ssid_t;

enum CBW {
    // Channel Bandwidth. See IEEE 802.11-2016 21.2.4 Table 21-2
    // VHT notation

    CBW20 = 0,  // Default. Corresponds to SecondaryChannelOffset-None
    CBW40 = 1,
    CBW40ABOVE = CBW40,  // Corresponds to SecondaryChannelOffset-Above
    CBW40BELOW = 2,      // Corresponds to SecondaryChannelOffset-Below
    CBW80 = 3,
    CBW160 = 4,
    CBW80P80 = 5,  // Non-contiguous frequency segments
};

typedef struct wlan_channel {
    uint8_t primary;
    uint8_t cbw;          // Channel Bandwidth
    uint8_t secondary80;  // Channel index corresponding to the center frequency
                          // of the secondary frequency segment
} wlan_channel_t;

enum {
    WLAN_RX_INFO_VALID_PHY = (1 << 0),
    WLAN_RX_INFO_VALID_DATA_RATE = (1 << 1),
    WLAN_RX_INFO_VALID_CHAN_WIDTH = (1 << 2),
    WLAN_RX_INFO_VALID_MCS = (1 << 3),
    WLAN_RX_INFO_VALID_RSSI = (1 << 4),
    WLAN_RX_INFO_VALID_RCPI = (1 << 5),
    WLAN_RX_INFO_VALID_SNR = (1 << 6),
    // Bits 7-31 reserved
};

// Guard Interval
enum GI {
    WLAN_GI_800NS = (1 << 0),   // all 802.11 phy
    WLAN_GI_400NS = (1 << 1),   // 802.11n/ac
    WLAN_GI_200NS = (1 << 2),   // 802.11n/ac
    WLAN_GI_3200NS = (1 << 3),  // 802.11ax
    WLAN_GI_1600NS = (1 << 4),  // 802.11ax
};

enum {
    WLAN_BSS_TYPE_INFRASTRUCTURE = 1,
    WLAN_BSS_TYPE_IBSS = 2,  // Independent BSS
    WLAN_BSS_TYPE_PERSONAL = 3,
    WLAN_BSS_TYPE_MESH = 4,
    WLAN_BSS_TYPE_ANY_BSS = 5,
};

typedef struct wlan_bss_config {
    uint8_t bssid[6];
    // Whether this BSS is an infrastructure or independent BSS.
    uint8_t bss_type;
    // If 'remote' is 'true', the BSS is *not* managed by this device.
    bool remote;
} wlan_bss_config_t;

// HT Operation. IEEE Std 802.11-2016,
typedef struct wlan_ht_op {
    uint8_t primary_chan;
    union {
        uint8_t info[5];
        struct {
            uint32_t head;
            uint8_t tail;
        } __PACKED;
    };
    union {
        uint8_t supported_mcs_set[16];
        struct {
            uint64_t rx_mcs_head;
            uint32_t rx_mcs_tail;
            uint32_t tx_mcs;
        } __PACKED basic_mcs_set;
    };
} __PACKED wlan_ht_op_t;

// VHT Operation. IEEE Std 802.11-2016, 9.4.2.159
typedef struct wlan_vht_op {
    uint8_t vht_cbw;
    uint8_t center_freq_seg0;
    uint8_t center_freq_seg1;
    uint16_t basic_mcs;
} __PACKED wlan_vht_op_t;

// Information defined only within a context of association
// Beware the subtle interpretation of each field: they are designed to
// reflect the parameters safe to use within an association
// Many parameters do not distinguish Rx capability from Tx capability.
// In those cases, a capability is commonly applied to both Rx and Tx.
// Some parameters are distinctively for Rx only, and some are Tx only.
#define WLAN_MAC_MAX_SUPP_RATES 8
#define WLAN_MAC_MAX_EXT_RATES 255
#define WLAN_MAC_MAX_RATES (8 + 255)
typedef struct wlan_assoc_ctx {
    uint8_t bssid[6];
    uint16_t aid;
    uint16_t listen_interval;
    uint8_t phy;  // a single enumerator from enum PHY.
    wlan_channel_t chan;
    bool qos;

    // Coincatenation of SupportedRates and ExtendedSupportedRates
    // IEEE Std 802.11-2016, 9.4.2.3 & 9.4.2.13
    uint16_t rates_cnt;
    uint8_t rates[WLAN_MAC_MAX_RATES];

    // IEEE Std 802.11-2016, 9.4.1.4
    uint8_t cap_info[2];

    // IEEE Std 802.11-2016, 9.4.2.56, 57
    // Rx MCS Bitmask in Supported MCS Set field represents the set of MCS
    // the peer can receive at from this device, considering this device's Tx capability.
    bool has_ht_cap;
    ieee80211_ht_capabilities_t ht_cap;
    bool has_ht_op;
    wlan_ht_op_t ht_op;

    // IEEE Std 802.11-2016, 9.4.2.158, 159
    bool has_vht_cap;
    ieee80211_vht_capabilities_t vht_cap;
    bool has_vht_op;
    wlan_vht_op_t vht_op;
} wlan_assoc_ctx_t;

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_
