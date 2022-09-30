// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of banjo definitions that were auto generated from
// fuchsia.hardware.wlan.softmac. Since banjo is being deprecated, we are making a local copy of
// defines that the driver relies upon.fxbug.dev/104598 is the tracking bug to remove the usage
// of platforms/banjo/*.h files.

// WARNING: DO NOT ADD MORE DEFINITIONS TO THIS FILE

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_SOFTMAC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_SOFTMAC_H_

#include <zircon/types.h>

#include "common.h"

// Forward declarations
typedef uint8_t wlan_tx_info_valid_t;
#define WLAN_TX_INFO_VALID_DATA_RATE UINT8_C(0x1)
#define WLAN_TX_INFO_VALID_TX_VECTOR_IDX UINT8_C(0x2)
#define WLAN_TX_INFO_VALID_PHY UINT8_C(0x4)
#define WLAN_TX_INFO_VALID_CHANNEL_BANDWIDTH UINT8_C(0x8)
#define WLAN_TX_INFO_VALID_MCS UINT8_C(0x10)
typedef uint8_t wlan_tx_info_flags_t;
#define WLAN_TX_INFO_FLAGS_PROTECTED UINT8_C(0x1)
#define WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY UINT8_C(0x2)
#define WLAN_TX_INFO_FLAGS_QOS UINT8_C(0x4)
typedef struct wlan_tx_info wlan_tx_info_t;
typedef struct wlan_tx_packet wlan_tx_packet_t;
typedef uint32_t wlan_rx_info_flags_t;
#define WLAN_RX_INFO_FLAGS_FCS_INVALID UINT32_C(0x1)
#define WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4 UINT32_C(0x2)
typedef struct wlan_rx_info wlan_rx_info_t;
typedef struct wlan_rx_packet wlan_rx_packet_t;
typedef struct wlan_softmac_ifc_protocol_ops wlan_softmac_ifc_protocol_ops_t;

// Declarations
struct wlan_tx_info {
  // Transmit flags. These represent boolean options as opposed to enums or other value-based
  // info which are represented below. Values should be taken from the WLAN_TX_INFO_FLAGS_* enum.
  wlan_tx_info_flags_t tx_flags;
  // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
  // must be zero. Values for fields not indicated by a flag may be chosen at the discretion of
  // the softmac driver.
  uint32_t valid_fields;
  uint16_t tx_vector_idx;
  wlan_phy_type_t phy;
  channel_bandwidth_t channel_bandwidth;
  // The modulation and coding scheme index for this packet. Depends on the PHY format and
  // channel width.
  uint8_t mcs;
};

struct wlan_tx_packet {
  const uint8_t* mac_frame_buffer;
  size_t mac_frame_size;
  // Additional data needed to transmit the packet.
  wlan_tx_info_t info;
};
struct wlan_rx_info {
  // Boolean receive flags. Enums and value-based info are represented below.
  wlan_rx_info_flags_t rx_flags;
  // Bitmask indicating which of the following fields are valid in this struct. Reserved flags
  // must be zero.
  uint32_t valid_fields;
  // The PHY format of the device at the time of the operation.
  wlan_phy_type_t phy;
  // The data rate of the device, measured in units of 0.5 Mb/s.
  uint32_t data_rate;
  // The channel of the device at the time of the operation. This field must be included.
  wlan_channel_t channel;
  // The modulation and coding scheme index of the device at the time of the operation. Depends
  // on the PHY format and channel width.
  uint8_t mcs;
  // Received Signal Strength Indicator.
  int8_t rssi_dbm;
  // Signal-to-Noise Ratio, in 0.5 dB.
  int16_t snr_dbh;
};

struct wlan_rx_packet {
  const uint8_t* mac_frame_buffer;
  size_t mac_frame_size;
  wlan_rx_info_t info;
};

struct wlan_softmac_ifc_protocol_ops {
  void (*status)(void* ctx, uint32_t status);
  void (*recv)(void* ctx, const wlan_rx_packet_t* packet);
  void (*complete_tx)(void* ctx, const wlan_tx_packet_t* packet, zx_status_t status);
  void (*report_tx_status)(void* ctx, const wlan_tx_status_t* tx_status);
  void (*scan_complete)(void* ctx, zx_status_t status, uint64_t scan_id);
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_SOFTMAC_H_
