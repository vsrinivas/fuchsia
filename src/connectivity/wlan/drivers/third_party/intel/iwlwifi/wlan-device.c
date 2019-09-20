// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the code to interact with the MLME.
//
//          MLME
//            |
//            v
//  +-------------------+
//  |   wlan-device.c   |
//  +-------------------+
//  | PHY ops | MAC ops |
//  +-------------------+
//       |         |
//       v         v
//     mvm/mac80211.c
//
// Note that the |*ctx| in this file is actually |*iwl_trans| passed when device_add() is called.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

#include <stdio.h>
#include <string.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

static zx_status_t phy_query(void* ctx, wlanphy_impl_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Returns dummy info for now.
  memset(info, 0, sizeof(*info));

  // TODO(fxb/36682): reads real MAC address from hardware.
  uint8_t fake_mac[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  memcpy(info->wlan_info.mac_addr, fake_mac, sizeof(info->wlan_info.mac_addr));

  // TODO(fxb/36677): supports AP role
  info->wlan_info.mac_role = WLAN_INFO_MAC_ROLE_CLIENT;

  info->wlan_info.supported_phys =
      WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_CCK | WLAN_INFO_PHY_TYPE_OFDM;
  // TODO(fxb/36683): supports HT (802.11n): WLAN_INFO_PHY_TYPE_HT
  // TODO(fxb/36684): suuports VHT (802.11ac): WLAN_INFO_PHY_TYPE_VHT

  // The current band/channel setting is for channel 11 only (in 2.4GHz).
  // TODO(fxb/36689): lists all bands and their channels.
  wlan_info_band_info_t* wlan_band = &info->wlan_info.bands[info->wlan_info.bands_count++];
  wlan_band->band = WLAN_INFO_BAND_2GHZ;
  // See IEEE Std 802.11-2016, 9.4.2.3 for encoding. Those values are:
  //   [1Mbps, 2Mbps, 5.5Mbps, 11Mbps, 6Mbps, 9Mbps, 12Mbps, 18Mbps, 24Mbps, 36Mbps, 48Mbps, 54Mbps]
  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
  static_assert(sizeof(rates) <= sizeof(wlan_band->basic_rates), "Too many basic_rates to copy");
  memcpy(wlan_band->basic_rates, rates, sizeof(rates));
  wlan_band->supported_channels.base_freq = 2407;
  wlan_band->supported_channels.channels[0] = 11;

  return ZX_OK;
}

static zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                                    uint16_t* out_iface_id) {
  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t phy_destroy_iface(void* ctx, uint16_t id) {
  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country) {
  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

// PHY interface
wlanphy_impl_protocol_ops_t wlanphy_ops = {
    .query = phy_query,
    .create_iface = phy_create_iface,
    .destroy_iface = phy_destroy_iface,
    .set_country = phy_set_country,
};
