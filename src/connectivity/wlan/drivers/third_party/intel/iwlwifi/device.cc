// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/device.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

namespace wlan {
namespace iwlwifi {

zx_status_t Device::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  return phy_query(iwl_trans_, out_info);
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  return phy_create_iface(iwl_trans_, req, out_iface_id);
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  return phy_destroy_iface(iwl_trans_, iface_id);
}

zx_status_t Device::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  return phy_set_country(iwl_trans_, country);
}

zx_status_t Device::WlanphyImplClearCountry() {
  IWL_ERR(ctx, "%s() not implemented ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplGetCountry(wlanphy_country_t* out_country) {
  return phy_get_country(iwl_trans_, out_country);
}

}  // namespace iwlwifi
}  // namespace wlan
