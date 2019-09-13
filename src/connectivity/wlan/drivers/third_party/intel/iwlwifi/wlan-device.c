// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the code to interact with the MLME.
//
// Note that the |*ctx| in this file is actually |*iwl_trans| passed when device_add() is called.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

#include <stdio.h>
#include <string.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"

static zx_status_t phy_query(void* ctx, wlanphy_impl_info_t* info) {
  // Returns dummy info for now.
  memset(info, 0, sizeof(*info));
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
