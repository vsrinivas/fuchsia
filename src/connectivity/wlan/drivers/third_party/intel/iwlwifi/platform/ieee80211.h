/*
 * Copyright 2019 The Fuchsia Authors.
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

// TODO(29700): Consolidate to one ieee80211.h

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IEEE80211_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IEEE80211_H_

#include <netinet/if_ether.h>
#include <stddef.h>
#include <stdint.h>

#include <wlan/common/ieee80211.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/softmac.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// The below constants are not defined in the 802.11-2016 Std.
#define IEEE80211_MAX_CHAINS 4
#define IEEE80211_MAX_RTS_THRESHOLD 2353
#define IEEE80211_MAC_PACKET_HEADROOM_SIZE 8

// Used as the default value in the data structure to indicate the queue is not set yet.
#define IEEE80211_INVAL_HW_QUEUE 0xff

// Convert the TID sequence number into the SSN (start sequence number) in the BAR (Block Ack
// Request).
#define IEEE80211_SCTL_SEQ_MASK 0xfff
#define IEEE80211_SCTL_SEQ_OFFSET 4
#define IEEE80211_SEQ_TO_SN(seq) (((seq) >> IEEE80211_SCTL_SEQ_OFFSET) & IEEE80211_SCTL_SEQ_MASK)

/* 802.11n HT capabilities masks (for cap_info) */
#define IEEE80211_HT_CAP_LDPC_CODING 0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40 0x0002
#define IEEE80211_HT_CAP_SM_PS 0x000C
#define IEEE80211_HT_CAP_SM_PS_SHIFT 2
#define IEEE80211_HT_CAP_GRN_FLD 0x0010
#define IEEE80211_HT_CAP_SGI_20 0x0020
#define IEEE80211_HT_CAP_SGI_40 0x0040
#define IEEE80211_HT_CAP_TX_STBC 0x0080
#define IEEE80211_HT_CAP_RX_STBC 0x0300
#define IEEE80211_HT_CAP_RX_STBC_SHIFT 8
#define IEEE80211_HT_CAP_DELAY_BA 0x0400
#define IEEE80211_HT_CAP_MAX_AMSDU 0x0800
#define IEEE80211_HT_CAP_DSSSCCK40 0x1000
#define IEEE80211_HT_CAP_RESERVED 0x2000
#define IEEE80211_HT_CAP_40MHZ_INTOLERANT 0x4000
#define IEEE80211_HT_CAP_LSIG_TXOP_PROT 0x8000

/* 802.11n HT capability MSC set */
#define IEEE80211_HT_MCS_RX_HIGHEST_MASK 0x3ff
#define IEEE80211_HT_MCS_TX_DEFINED 0x01
#define IEEE80211_HT_MCS_TX_RX_DIFF 0x02

#define IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK 0x0C
#define IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT 2
#define IEEE80211_HT_MCS_TX_MAX_STREAMS 4
#define IEEE80211_HT_MCS_TX_UNEQUAL_MODULATION 0x10

// Ids of information elements referred in this driver.
#define WLAN_EID_SSID 0

// The order of access categories is not clearly specified in 802.11-2016 Std.
// Therefore it cannot be moved into ieee80211 banjo file.
enum ieee80211_ac_numbers {
  IEEE80211_AC_VO = 0,
  IEEE80211_AC_VI = 1,
  IEEE80211_AC_BE = 2,
  IEEE80211_AC_BK = 3,
  IEEE80211_AC_MAX = 4,
};

enum ieee80211_frame_release_type {
  IEEE80211_FRAME_RELEASE_PSPOLL,
  IEEE80211_FRAME_RELEASE_UAPSD,
};

// IEEE Std 802.11-2016, 9.4.2.56.3, Table 9-163
enum ieee80211_max_ampdu_length_exp {
  IEEE80211_HT_MAX_AMPDU_8K = 0,
  IEEE80211_HT_MAX_AMPDU_16K = 1,
  IEEE80211_HT_MAX_AMPDU_32K = 2,
  IEEE80211_HT_MAX_AMPDU_64K = 3
};

/* Minimum MPDU start spacing */
enum ieee80211_min_mpdu_spacing {
  IEEE80211_HT_MPDU_DENSITY_NONE = 0, /* No restriction */
  IEEE80211_HT_MPDU_DENSITY_0_25 = 1, /* 1/4 usec */
  IEEE80211_HT_MPDU_DENSITY_0_5 = 2,  /* 1/2 usec */
  IEEE80211_HT_MPDU_DENSITY_1 = 3,    /* 1 usec */
  IEEE80211_HT_MPDU_DENSITY_2 = 4,    /* 2 usec */
  IEEE80211_HT_MPDU_DENSITY_4 = 5,    /* 4 usec */
  IEEE80211_HT_MPDU_DENSITY_8 = 6,    /* 8 usec */
  IEEE80211_HT_MPDU_DENSITY_16 = 7    /* 16 usec */
};

enum ieee80211_roc_type {
  IEEE80211_ROC_TYPE_NORMAL = 0,
  IEEE80211_ROC_TYPE_MGMT_TX,
};

enum ieee80211_rssi_event_data {
  RSSI_EVENT_HIGH,
  RSSI_EVENT_LOW,
};

enum ieee80211_smps_mode {
  IEEE80211_SMPS_AUTOMATIC,
  IEEE80211_SMPS_OFF,
  IEEE80211_SMPS_STATIC,
  IEEE80211_SMPS_DYNAMIC,
  IEEE80211_SMPS_NUM_MODES,
};

// NEEDS_PORTING: Below structures are only referenced in function prototype.
//                Doesn't need a dummy byte.
struct cfg80211_gtk_rekey_data;
struct cfg80211_nan_conf;
struct cfg80211_nan_func;
struct cfg80211_scan_request;
struct cfg80211_sched_scan_request;
struct cfg80211_wowlan;
struct ieee80211_key_conf;
struct ieee80211_sta_ht_cap;
struct ieee80211_scan_ies;
struct ieee80211_tdls_ch_sw_params;

// NEEDS_PORTING: Below structures are used in code but not ported yet.
// A dummy byte is required to suppress the C++ warning message for empty
// struct.
struct cfg80211_chan_def {
  char dummy;
};

struct ieee80211_hdr {
  char dummy;
};

struct ieee80211_ops {
  char dummy;
};

struct ieee80211_p2p_noa_desc {
  char dummy;
};

// Channel info. Attributes of a channel.
struct ieee80211_channel {
  wlan_band_t band;
  uint32_t center_freq;  // unit: MHz.
  uint16_t ch_num;       // channel number (starts from 1).
  uint32_t flags;
  int max_power;
};

struct ieee80211_mcs_info {
  uint8_t rx_mask[IEEE80211_HT_MCS_MASK_LEN];
  __le16 rx_highest_le;
  uint8_t tx_params;
  uint8_t reserved[3];
} __packed;

struct ieee80211_sta_ht_cap {
  uint16_t cap; /* use IEEE80211_HT_CAP_ */
  bool ht_supported;
  uint8_t ampdu_factor;
  uint8_t ampdu_density;
  struct ieee80211_mcs_info mcs;
};

struct ieee80211_supported_band {
  wlan_band_t band;
  struct ieee80211_channel* channels;
  size_t n_channels;
  uint16_t* bitrates;
  size_t n_bitrates;
  struct ieee80211_sta_ht_cap ht_cap;
};

struct ieee80211_tx_queue_params {
  uint16_t txop;
  uint16_t cw_min;
  uint16_t cw_max;
  uint8_t aifs;
};

struct ieee80211_tx_rate {
  char dummy;
};

struct ieee80211_txq;
struct ieee80211_sta {
  void* drv_priv;
  struct ieee80211_txq* txq[fuchsia_wlan_ieee80211_TIDS_MAX + 1];
};

struct ieee80211_txq {
  void* drv_priv;
};

// TODO(43559): completely remove this structure from code.
struct ieee80211_vif {
  uint8_t dummy;
};

/**
 * struct ieee80211_key_conf - HW key configuration data
 * @tx_pn - TX packet number, in host byte order
 * @rx_seq - RX sequence number, in host byte order
 */
struct ieee80211_key_conf {
  atomic64_t tx_pn;
  uint64_t rx_seq;
  uint32_t cipher;
  uint8_t hw_key_idx;
  uint8_t keyidx;
  uint8_t key_type;
  size_t keylen;
  uint8_t key[0];
};

struct ieee80211_tx_info {
  struct {
    struct ieee80211_key_conf* hw_key;
  } control;
};

// Struct for transferring an IEEE 802.11 MAC-framed packet around the driver.
struct ieee80211_mac_packet {
  // The common portion of the MAC header.
  const struct ieee80211_frame_header* common_header;

  // The size of the entire MAC header (starting at common_header), including variable fields.
  size_t header_size;

  // Statically allocated headroom space between the MAC header and frame body, for adding
  // additional headers to the packet.
  uint8_t headroom[IEEE80211_MAC_PACKET_HEADROOM_SIZE];

  // Size of the headroom used.
  size_t headroom_used_size;

  // MAC frame body.
  const uint8_t* body;

  // MAC frame body size.
  size_t body_size;

  // Control information for this packet.
  struct ieee80211_tx_info info;
};

// Flags for the ieee80211_rx_status.flag
enum ieee80211_rx_status_flags {
  RX_FLAG_DECRYPTED = 0x1,
  RX_FLAG_PN_VALIDATED = 0x2,
  RX_FLAG_ALLOW_SAME_PN = 0x4,
};

struct ieee80211_rx_status {
  // RX flags, as in Linux.
  uint64_t flag;

  // The encryption IV, copied here since the encryption header is removed for Fuchsia.
  uint8_t extiv[8];

  // RX info struct to pass to wlanstack.
  struct wlan_rx_info rx_info;
};

size_t ieee80211_get_header_len(const struct ieee80211_frame_header* fh);

struct ieee80211_hw* ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops* ops);

bool ieee80211_is_valid_chan(uint8_t primary);

uint16_t ieee80211_get_center_freq(uint8_t channel_num);

bool ieee80211_has_protected(const struct ieee80211_frame_header* fh);

bool ieee80211_is_data(const struct ieee80211_frame_header* fh);

bool ieee80211_is_data_qos(const struct ieee80211_frame_header* fh);

uint8_t ieee80211_get_tid(const struct ieee80211_frame_header* fh);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_IEEE80211_H_
