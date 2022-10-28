// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_H_

#include <net/ethernet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

// clang-format off
// IEEE Std 802.11-2016, 9.2.3
struct ieee80211_frame_header {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t addr1[ETH_ALEN];
    uint8_t addr2[ETH_ALEN];
    uint8_t addr3[ETH_ALEN];
    uint16_t seq_ctrl;
    uint8_t addr4[0];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.7
struct ieee80211_assoc_resp {
    uint16_t capabilities;
    uint16_t status;
    uint16_t assoc_id;
    uint8_t info[0];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
struct ieee80211_ht_info {
    uint8_t primary_channel;
    uint8_t ht_operation_info[5];
    uint8_t rx_mcs[10];
} __PACKED;

// IEEE Std 802.11-2016, 9.2.4.1.3
enum ieee80211_frame_type {
    IEEE80211_FRAME_TYPE_MGMT = 0x0,
    IEEE80211_FRAME_TYPE_CTRL = 0x4,
    IEEE80211_FRAME_TYPE_DATA = 0x8,
    IEEE80211_FRAME_TYPE_EXT  = 0xc,
};

// IEEE Std 802.11-2016, 9.2.4.1.3
enum ieee80211_frame_subtype {
    /* MGMT */
    IEEE80211_FRAME_SUBTYPE_ASSOC_REQ  = 0x00,
    IEEE80211_FRAME_SUBTYPE_ASSOC_RESP = 0x10,
    IEEE80211_FRAME_SUBTYPE_BEACON     = 0x80,
    IEEE80211_FRAME_SUBTYPE_DISASSOC   = 0xa0,
    IEEE80211_FRAME_SUBTYPE_DEAUTH     = 0xc0,
    IEEE80211_FRAME_SUBTYPE_ACTION     = 0xd0,

    /* DATA */
    IEEE80211_FRAME_SUBTYPE_PROBE_RESP = 0x50,
    IEEE80211_FRAME_SUBTYPE_QOS        = 0x80,
    IEEE80211_FRAME_SUBTYPE_QOS_NULL   = 0xc0,

    /* CONTROL */
    IEEE80211_FRAME_SUBTYPE_BACK_REQ   = 0x80,
    IEEE80211_FRAME_SUBTYPE_CTS        = 0xc0,
    IEEE80211_FRAME_SUBTYPE_ACK        = 0xd0,
};

// IEEE Std 802.11-2016, 9.2.4.1.1
#define IEEE80211_FRAME_TYPE_MASK                   0x000c
#define IEEE80211_FRAME_SUBTYPE_MASK                0x00f0
#define IEEE80211_FRAME_CTRL_TO_DS_MASK             0x0100
#define IEEE80211_FRAME_CTRL_FROM_DS_MASK           0x0200
#define IEEE80211_FRAME_CTRL_MORE_FRAGMENTS_MASK    0x0400
#define IEEE80211_FRAME_PROTECTED_MASK              0x4000
#define IEEE80211_FRAME_CTRL_HTC_ORDER_MASK         0x8000

// IEEE Std 802.11-2016, 9.2.4.5.1
#define IEEE80211_QOS_CTRL_A_MSDU_PRESENT   0x0080

static inline enum ieee80211_frame_type
ieee80211_get_frame_type(const struct ieee80211_frame_header* fh) {
    return (enum ieee80211_frame_type)(fh->frame_ctrl & IEEE80211_FRAME_TYPE_MASK);
}

static inline enum ieee80211_frame_subtype
ieee80211_get_frame_subtype(const struct ieee80211_frame_header* fh) {
    return (enum ieee80211_frame_subtype)(fh->frame_ctrl & IEEE80211_FRAME_SUBTYPE_MASK);
}

static inline bool ieee80211_pkt_is_protected(const struct ieee80211_frame_header* fh) {
    return fh->frame_ctrl & IEEE80211_FRAME_PROTECTED_MASK;
}

static inline uint8_t* ieee80211_get_mgmt_bssid(struct ieee80211_frame_header* fh) {
    if (ieee80211_get_frame_type(fh) == IEEE80211_FRAME_TYPE_MGMT) {
        return fh->addr3;
    }
    return NULL;
}

static bool ieee80211_has_addr4(const struct ieee80211_frame_header* hdr) {
    uint16_t mask = IEEE80211_FRAME_CTRL_TO_DS_MASK | IEEE80211_FRAME_CTRL_FROM_DS_MASK;
    return (hdr->frame_ctrl & mask) == mask;
}

static inline size_t ieee80211_is_qos_data(const struct ieee80211_frame_header* fh) {
    return ieee80211_get_frame_type(fh) == IEEE80211_FRAME_TYPE_DATA
        && (ieee80211_get_frame_subtype(fh) & IEEE80211_FRAME_SUBTYPE_QOS);
}

static inline size_t ieee80211_get_qos_ctrl_offset(const struct ieee80211_frame_header* hdr) {
    return sizeof(struct ieee80211_frame_header) + (ieee80211_has_addr4(hdr) ? ETH_ALEN : 0);
}

static inline size_t ieee80211_hdrlen(const struct ieee80211_frame_header* fh) {
    switch (ieee80211_get_frame_type(fh)) {
    case IEEE80211_FRAME_TYPE_MGMT:
    case IEEE80211_FRAME_TYPE_DATA:
        return sizeof(struct ieee80211_frame_header)
            + (ieee80211_has_addr4(fh) ? ETH_ALEN : 0)
            + (ieee80211_is_qos_data(fh) ? 2 : 0)
            + ((fh->frame_ctrl & IEEE80211_FRAME_CTRL_HTC_ORDER_MASK) ? 4 : 0);
    case IEEE80211_FRAME_TYPE_CTRL:
        // See IEEE Std. 802.11-2016, 9.3.1
        switch (ieee80211_get_frame_subtype(fh)) {
        case IEEE80211_FRAME_SUBTYPE_CTS:
        case IEEE80211_FRAME_SUBTYPE_ACK:
            return 10;
        default:
            return 16;
        }
    default:
        return sizeof(struct ieee80211_frame_header);
    }
}

static inline uint8_t* ieee80211_get_dest_addr(struct ieee80211_frame_header* fh) {
    if (fh->frame_ctrl & IEEE80211_FRAME_CTRL_TO_DS_MASK) {
        return fh->addr3;
    } else {
        return fh->addr1;
    }
}

static inline uint8_t* ieee80211_get_src_addr(struct ieee80211_frame_header* fh) {
    uint16_t mask = IEEE80211_FRAME_CTRL_TO_DS_MASK | IEEE80211_FRAME_CTRL_FROM_DS_MASK;
    switch (fh->frame_ctrl & mask) {
    case IEEE80211_FRAME_CTRL_TO_DS_MASK | IEEE80211_FRAME_CTRL_FROM_DS_MASK:
        return fh->addr4;
    case IEEE80211_FRAME_CTRL_FROM_DS_MASK:
        return fh->addr3;
    default:
        return fh->addr2;
    }
}

#define IEEE80211_HT_MAX_AMPDU_FACTOR 13

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
enum ieee80211_assoc_tags {
    IEEE80211_ASSOC_TAG_SSID = 0,
    IEEE80211_ASSOC_TAG_RATES = 1,
    IEEE80211_ASSOC_TAG_HT_CAPS = 45,
    IEEE80211_ASSOC_TAG_EXTENDED_RATES = 50,
    IEEE80211_ASSOC_TAG_HT_INFO = 61,
};

// IEEE Std 802.11-2016, 9.4.2.56.2, Figure 9-332
enum ieee80211_ht_caps {
    IEEE80211_HT_CAPS_LDPC              = 0x0001,
    IEEE80211_HT_CAPS_CHAN_WIDTH        = 0x0002,
    IEEE80211_HT_CAPS_SMPS              = 0x000c,
        IEEE80211_HT_CAPS_SMPS_STATIC   = 0x0000,
        IEEE80211_HT_CAPS_SMPS_DYNAMIC  = 0x0004,
        IEEE80211_HT_CAPS_SMPS_DISABLED = 0x000c,
        IEEE80211_HT_CAPS_SMPS_SHIFT    = 2,
    IEEE80211_HT_CAPS_GF                = 0x0010,
    IEEE80211_HT_CAPS_SGI_20            = 0x0020,
    IEEE80211_HT_CAPS_SGI_40            = 0x0040,
    IEEE80211_HT_CAPS_TX_STBC           = 0x0080,
    IEEE80211_HT_CAPS_RX_STBC           = 0x0300,
        IEEE80211_HT_CAPS_RX_STBC_SHIFT = 8,
    IEEE80211_HT_CAPS_DELAYED_BLOCK_ACK = 0x0400,
    IEEE80211_HT_CAPS_MAX_AMSDU_LEN     = 0x0800,
    IEEE80211_HT_CAPS_DSSS_CCK_40       = 0x1000,
    /* RESERVED - bit 13 */
    IEEE80211_HT_CAPS_40_INTOLERANT     = 0x4000,
    IEEE80211_HT_CAPS_L_SIG_TXOP_PROT   = 0x8000
};

#define IEEE80211_HT_MCS_MASK_LEN 10

// IEEE Std 802.11-2016 9.4.2.56.3, Figure 9-333
enum ieee80211_ampdu_params {
    IEEE80211_AMPDU_RX_LEN_MASK          = 0x03,
        IEEE80211_AMPDU_RX_LEN_SHIFT     = 0,
    IEEE80211_AMPDU_DENSITY_MASK         = 0x1c,
        IEEE80211_AMPDU_DENSITY_SHIFT    = 2
};

// IEEE Std 802.11-2016 9.4.2.56.1, Figure 9-331
// This struct should only be used in files with C linkage where the
// more expressive wlan::HtCapabilities is unavailable.
struct ieee80211_ht_cap_packed {
  uint16_t ht_capability_info;
  uint8_t ampdu_params;
  uint8_t supported_mcs_set[16];
  //struct ieee80211_mcs_info supported_mcs_set;
  uint16_t ht_ext_cap;
  uint32_t txbf_cap;
  uint8_t asel_cap;
} __PACKED;

// IEEE Std. 802.11-2016 9.4.2.158.2
enum ieee80211_vht_caps {
    IEEE80211_VHT_CAPS_MAX_MPDU_LEN          =        0x3,
    IEEE80211_VHT_CAPS_MAX_MPDU_LEN_SHIFT    =          0,
    IEEE80211_VHT_CAPS_SUPP_CHAN_WIDTH       =        0xc,
    IEEE80211_VHT_CAPS_SUPP_CHAN_WIDTH_SHIFT =          2,
    IEEE80211_VHT_CAPS_RX_LDPC               =       0x10,
    IEEE80211_VHT_CAPS_RX_LDPC_SHIFT         =          4,
    IEEE80211_VHT_CAPS_SGI_80                =       0x20,
    IEEE80211_VHT_CAPS_SGI_80_SHIFT          =          5,
    IEEE80211_VHT_CAPS_SGI_160               =       0x40,
    IEEE80211_VHT_CAPS_SGI_160_SHIFT         =          6,
    IEEE80211_VHT_CAPS_TX_STBC               =       0x80,
    IEEE80211_VHT_CAPS_TX_STBC_SHIFT         =          7,
    IEEE80211_VHT_CAPS_RX_STBC               =      0x700,
    IEEE80211_VHT_CAPS_RX_STBC_SHIFT         =          8,
    IEEE80211_VHT_CAPS_SU_BEAMFORMER         =      0x800,
    IEEE80211_VHT_CAPS_SU_BEAMFORMER_SHIFT   =         11,
    IEEE80211_VHT_CAPS_SU_BEAMFORMEE         =     0x1000,
    IEEE80211_VHT_CAPS_SU_BEAMFORMEE_SHIFT   =         12,
    IEEE80211_VHT_CAPS_BEAMFORMEE_STS        =     0xe000,
    IEEE80211_VHT_CAPS_BEAMFORMEE_STS_SHIFT  =         13,
    IEEE80211_VHT_CAPS_SOUND_DIM             =    0x70000,
    IEEE80211_VHT_CAPS_SOUND_DIM_SHIFT       =         16,
    IEEE80211_VHT_CAPS_MU_BEAMFORMER         =    0x80000,
    IEEE80211_VHT_CAPS_MU_BEAMFORMER_SHIFT   =         19,
    IEEE80211_VHT_CAPS_MU_BEAMFORMEE         =   0x100000,
    IEEE80211_VHT_CAPS_MU_BEAMFORMEE_SHIFT   =         20,
    IEEE80211_VHT_CAPS_TXOP_PS               =   0x200000,
    IEEE80211_VHT_CAPS_TXOP_PS_SHIFT         =         21,
    IEEE80211_VHT_CAPS_HTC_VHT               =   0x400000,
    IEEE80211_VHT_CAPS_HTC_VHT_SHIFT         =         22,
    IEEE80211_VHT_CAPS_MAX_AMPDU_LEN         =  0x3800000,
    IEEE80211_VHT_CAPS_MAX_AMPDU_LEN_SHIFT   =         23,
    IEEE80211_VHT_CAPS_VHT_LINK_ADAPT        =  0xc000000,
    IEEE80211_VHT_CAPS_VHT_LINK_ADAPT_SHIFT  =         26,
    IEEE80211_VHT_CAPS_RX_ANT_CONSIST        = 0x10000000,
    IEEE80211_VHT_CAPS_RX_ANT_CONSIST_SHIFT  =         28,
    IEEE80211_VHT_CAPS_TX_ANT_CONSIST        = 0x20000000,
    IEEE80211_VHT_CAPS_TX_ANT_CONSIST_SHIFT  =         29,
    IEEE80211_VHT_CAPS_EXT_NSS_BW            = 0xc0000000,
    IEEE80211_VHT_CAPS_EXT_NSS_BW_SHIFT      =         30
};

// IEEE Std. 802.11-2016 9.4.2.158.3
enum ieee80211_supported_vht_mcs_nss {
    IEEE80211_VHT_MCS_NSS_RX_MCS_MAP            =             0xffff,
    IEEE80211_VHT_MCS_NSS_RX_MCS_MAP_SHIFT      =                  0,
    IEEE80211_VHT_MCS_NSS_RX_MAX_LGI_RATE       =         0x1fff0000,
    IEEE80211_VHT_MCS_NSS_RX_MAX_LGI_RATE_SHIFT =                 16,
    IEEE80211_VHT_MCS_NSS_MAX_NSTS              =         0xe0000000,
    IEEE80211_VHT_MCS_NSS_MAX_NSTS_SHIFT        =                 29,
    IEEE80211_VHT_MCS_NSS_TX_MCS_MAP            =     0xffff00000000,
    IEEE80211_VHT_MCS_NSS_TX_MCS_MAP_SHIFT      =                 32,
    IEEE80211_VHT_MCS_NSS_TX_MAX_LGI_RATE       = 0x1fff000000000000,
    IEEE80211_VHT_MCS_NSS_TX_MAX_LGI_RATE_SHIFT =                 48,
    IEEE80211_VHT_MCS_NSS_EXT_NSS_BW_CAP        = 0x2000000000000000,
    IEEE80211_VHT_MCS_NSS_EXT_NSS_BW_CAP_SHIFT  =                 61,
};

// IEEE Std 802.11-2016 9.4.2.158.1, Figure 9-558
// This struct should only be used in files with C linkage where the
// more expressive wlan::VhtCapabilities is unavailable.
struct ieee80211_vht_cap_packed {
  uint32_t vht_capability_info;
  uint64_t supported_vht_mcs_and_nss_set;
} __PACKED;

// Max VHT-MCS SS encodings (IEEE Std. 802.11-2016 9.4.2.158.3)
#define IEEE80211_VHT_MCS_0_7  0
#define IEEE80211_VHT_MCS_0_8  1
#define IEEE80211_VHT_MCS_0_9  2
#define IEEE80211_VHT_MCS_NONE 3

#define IEEE80211_QOS_CTL_LEN 2

#define IEEE80211_MSDU_SIZE_MAX 2304

// IEEE Std 802.11-2016, 9.2.4.8
#define IEEE80211_FCS_LEN 4

// IEEE Std 802.11-2016, 12.3.2.2
#define IEEE80211_WEP_IV_LEN 4
#define IEEE80211_WEP_ICV_LEN 4

// IEEE Std 802.11-2016, 12.5.2.2
#define IEEE80211_TKIP_IV_LEN 8
#define IEEE80211_TKIP_ICV_LEN 4

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131
enum ieee80211_cipher_suite {
    IEEE80211_CIPHER_SUITE_GROUP    = 0,
    IEEE80211_CIPHER_SUITE_WEP_40   = 1,
    IEEE80211_CIPHER_SUITE_TKIP     = 2,
    IEEE80211_CIPHER_SUITE_CCMP_128 = 4,
    IEEE80211_CIPHER_SUITE_WEP_104  = 5,
    IEEE80211_CIPHER_SUITE_CMAC_128 = 6,
    IEEE80211_CIPHER_SUITE_GCMP_128 = 8,
    IEEE80211_CIPHER_SUITE_GCMP_256 = 9,
    IEEE80211_CIPHER_SUITE_CCMP_256 = 10,
    IEEE80211_CIPHER_SUITE_GMAC_128 = 11,
    IEEE80211_CIPHER_SUITE_GMAC_256 = 12,
    IEEE80211_CIPHER_SUITE_CMAC_256 = 13,
    IEEE80211_CIPHER_SUITE_NONE     = 255,  // Not in the spec, a special value for driver to delete
                                            // a key.
};

static inline const char* ieee80211_cipher_str(const uint8_t* oui, uint8_t cipher_type) {
    if (oui[0] != 0 || oui[1] != 0x0f || oui[2] != 0xac) {
        return "vendor-specific OUI\n";
    }
    switch (cipher_type) {
    case IEEE80211_CIPHER_SUITE_GROUP:
        return "group";
    case IEEE80211_CIPHER_SUITE_WEP_40:
        return "WEP40";
    case IEEE80211_CIPHER_SUITE_TKIP:
        return "TKIP";
    case IEEE80211_CIPHER_SUITE_CCMP_128:
        return "CCMP128";
    case IEEE80211_CIPHER_SUITE_WEP_104:
        return "WEP104";
    case IEEE80211_CIPHER_SUITE_CMAC_128:
        return "CMAC_128";
    case IEEE80211_CIPHER_SUITE_GCMP_128:
        return "GCMP128";
    case IEEE80211_CIPHER_SUITE_GCMP_256:
        return "GCMP256";
    case IEEE80211_CIPHER_SUITE_CCMP_256:
        return "CCMP256";
    case IEEE80211_CIPHER_SUITE_GMAC_128:
        return "GMAC128";
    case IEEE80211_CIPHER_SUITE_GMAC_256:
        return "GMAC256";
    case IEEE80211_CIPHER_SUITE_CMAC_256:
        return "CMAC256";
    default:
        return "reserved CID value\n";
    }
}
// clang-format on

// Converts the from Time Unit (TU) to microseconds.
//
// Note that the returning value is in microsecond unit (not zx_duration_t). To convert it, use:
//
//   ZX_USEC(ieee80211_tu_to_usec(tu));
//
static inline int ieee80211_tu_to_usec(int tu) { return tu * 1024; }

#define IEEE80211_SN_MASK (0xFFF)
#define IEEE80211_SN_HALF_RANGE (0x800)

// Returns true if sn1 < sn2, conforming to rules specified by
// section 10.24.1 of IEEE Std 802.11-2016:
// sn2 is considered greater than sn1 if it's within 0x800 ahead of sn1,
// modulo 2^12.
static inline bool ieee80211_sn_less(uint16_t sn1, uint16_t sn2) {
    return (sn2 > sn1 && (sn1 + IEEE80211_SN_HALF_RANGE > sn2)) ||
           (sn1 > sn2 && (sn2 + IEEE80211_SN_HALF_RANGE < sn1));
}

// Adds two sequence numbers together, conforming to rules specified by
// section 10.24.1 of IEEE Std 802.11-2016.
static inline uint16_t ieee80211_sn_add(uint16_t sn1, uint16_t sn2) {
    return (sn1 + sn2) & IEEE80211_SN_MASK;
}

// Increments the given sequence number by 1.
static inline uint16_t ieee80211_sn_inc(uint16_t sn) { return ieee80211_sn_add(sn, 1); }

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_H_
