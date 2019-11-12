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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IEEE80211_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IEEE80211_H_

#include <stddef.h>
#include <stdint.h>

#include <ddk/hw/wlan/wlaninfo.h>

#define IEEE80211_CCMP_PN_LEN 6

#define IEEE80211_MAX_CHAINS 4

#define IEEE80211_MAX_SSID_LEN 32

#define IEEE80211_NUM_ACS 4

#define IEEE80211_NUM_TIDS 16

#define IEEE80211_MAX_RTS_THRESHOLD 2353

enum nl80211_chan_width {
  NL80211_CHAN_WIDTH_20_NOHT,
  NL80211_CHAN_WIDTH_20,
  NL80211_CHAN_WIDTH_40,
  NL80211_CHAN_WIDTH_80,
  NL80211_CHAN_WIDTH_80P80,
  NL80211_CHAN_WIDTH_160,
  NL80211_CHAN_WIDTH_5,
  NL80211_CHAN_WIDTH_10,
};

enum nl80211_iftype {
  NL80211_IFTYPE_UNSPECIFIED,
  NL80211_IFTYPE_ADHOC,
  NL80211_IFTYPE_STATION,
  NL80211_IFTYPE_AP,
  NL80211_IFTYPE_AP_VLAN,
  NL80211_IFTYPE_WDS,
  NL80211_IFTYPE_MONITOR,
  NL80211_IFTYPE_MESH_POINT,
  NL80211_IFTYPE_P2P_CLIENT,
  NL80211_IFTYPE_P2P_GO,
  NL80211_IFTYPE_P2P_DEVICE,
  NL80211_IFTYPE_OCB,
  NL80211_IFTYPE_NAN,

  /* keep last */
  NUM_NL80211_IFTYPES,
  NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1
};

enum ieee80211_ac_numbers {
  IEEE80211_AC_VO = 0,
  IEEE80211_AC_VI = 1,
  IEEE80211_AC_BE = 2,
  IEEE80211_AC_BK = 3,
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

enum ieee80211_sta_state {
  IEEE80211_STA_NOTEXIST,
  IEEE80211_STA_NONE,
  IEEE80211_STA_AUTH,
  IEEE80211_STA_ASSOC,
  IEEE80211_STA_AUTHORIZED,
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
struct ieee80211_rx_status;
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
  wlan_info_band_t band;
  uint32_t center_freq;  // unit: MHz.
  uint16_t ch_num;       // channel number (starts from 1).
  uint32_t flags;
  int max_power;
};

struct ieee80211_supported_band {
  wlan_info_band_t band;
  struct ieee80211_channel* channels;
  int n_channels;
  uint16_t* bitrates;
  int n_bitrates;
};

struct ieee80211_tx_queue_params {
  char dummy;
};

struct ieee80211_tx_rate {
  char dummy;
};

struct ieee80211_txq;
struct ieee80211_sta {
  void* drv_priv;
  struct ieee80211_txq* txq[IEEE80211_NUM_TIDS + 1];
};

struct ieee80211_tx_info {
  void* driver_data[8];
};

struct ieee80211_txq {
  void* drv_priv;
};

struct ieee80211_vif {
  void* drv_priv;
};

static inline struct ieee80211_hw* ieee80211_alloc_hw(size_t priv_data_len,
                                                      const struct ieee80211_ops* ops) {
  return NULL;  // NEEDS_PORTING
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IEEE80211_H_
