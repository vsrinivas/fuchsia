// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_

#include <stdint.h>

#include <zircon/compiler.h>

// LINT.IfChange

__BEGIN_CDECLS

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

__END_CDECLS

// LINT.ThenChange(//zircon/system/banjo/wlan.protocol.info/info.banjo)

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_INFO_H_
