/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _IEEE80211_H_
#define _IEEE80211_H_

#include <stdint.h>

#include "hw.h"

// IEEE Std 802.11-2016, 9.2.3
struct ieee80211_frame_header {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t addr1[ETH_ALEN];
    uint8_t addr2[ETH_ALEN];
    uint8_t addr3[ETH_ALEN];
    uint16_t seq_ctrl;
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.7
struct ieee80211_assoc_resp {
    uint16_t capabilities;
    uint16_t status;
    uint16_t assoc_id;
    uint8_t info[0];
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
    IEEE80211_FRAME_SUBTYPE_DISASSOC   = 0xa0,
    IEEE80211_FRAME_SUBTYPE_DEAUTH     = 0xc0,
    IEEE80211_FRAME_SUBTYPE_ACTION     = 0xd0,

    /* DATA */
    IEEE80211_FRAME_SUBTYPE_PROBE_RESP = 0x50,
    IEEE80211_FRAME_SUBTYPE_QOS        = 0x80,
    IEEE80211_FRAME_SUBTYPE_QOS_NULL   = 0xc0,
};

#define IEEE80211_FRAME_TYPE_MASK      0x000c
#define IEEE80211_FRAME_SUBTYPE_MASK   0x00f0
#define IEEE80211_FRAME_PROTECTED_MASK 0x4000

static inline enum ieee80211_frame_type
ieee80211_get_frame_type(const struct ieee80211_frame_header* fh) {
    return fh->frame_control & IEEE80211_FRAME_TYPE_MASK;
}

static inline enum ieee80211_frame_subtype
ieee80211_get_frame_subtype(const struct ieee80211_frame_header* fh) {
    return fh->frame_control & IEEE80211_FRAME_SUBTYPE_MASK;
}

static inline bool ieee80211_pkt_is_protected(const struct ieee80211_frame_header* fh) {
    return fh->frame_control & IEEE80211_FRAME_PROTECTED_MASK;
}

enum ieee80211_assoc_tags {
    IEEE80211_ASSOC_TAG_RATES = 1,
    IEEE80211_ASSOC_TAG_HT_CAPS = 45,
    IEEE80211_ASSOC_TAG_EXTENDED_RATES = 50,
};

enum ieee80211_ht_caps {
    IEEE80211_HT_CAPS_LDPC              = 0x0001,
    IEEE80211_HT_CAPS_20MHZ_ONLY        = 0x0002,
    IEEE80211_HT_CAPS_SMPS              = 0x000c,
    IEEE80211_HT_CAPS_GF                = 0x0010,
    IEEE80211_HT_CAPS_20_SGI            = 0x0020,
    IEEE80211_HT_CAPS_40_SGI            = 0x0040,
    IEEE80211_HT_CAPS_TX_STBC           = 0x0080,
    IEEE80211_HT_CAPS_RX_STBC           = 0x0300,
        IEEE80211_HT_CAPS_RX_STBC_SHIFT = 7,
    IEEE80211_HT_CAPS_DELAYED_BLOCK_ACK = 0x0400,
    IEEE80211_HT_CAPS_MAX_AMSDU_LEN     = 0x0800,
    IEEE80211_HT_CAPS_40_DSSS_CCX       = 0x1000,
    IEEE80211_HT_CAPS_PSMP              = 0x2000,
    IEEE80211_HT_CAPS_NO_40MHZ          = 0x4000,
    IEEE80211_HT_CAPS_L_SIG_TXOP_PROT   = 0x8000
};

enum ieee80211_a_mpdu_params {
    IEEE80211_A_MPDU_MAX_RX_LEN        = 0x03,
    IEEE80211_A_MPDU_DENSITY           = 0x1c,
        IEEE80211_A_MPDU_DENSITY_SHIFT = 2
};

#define IEEE80211_CCMP_MIC_LEN 8
#define IEEE80211_QOS_CTL_LEN 2

#endif /* _IEEE80211_H_ */
