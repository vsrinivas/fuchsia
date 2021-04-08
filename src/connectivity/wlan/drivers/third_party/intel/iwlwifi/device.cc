// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/device.h"

#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

namespace wlan::iwlwifi {

zx_status_t Device::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  return phy_query(iwl_trans_, out_info);
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  zx_status_t status = ZX_OK;

  fbl::AllocChecker ac;
  auto mac_device = fbl::make_unique_checked<MacDevice>(&ac, iwl_trans_->zxdev);
  if (!ac.check()) {
    IWL_ERR(this, "%s() failed to allocate mac_device (%zu bytes)", __func__, sizeof(*mac_device));
    return ZX_ERR_NO_MEMORY;
  }

  status = phy_create_iface(iwl_trans_, req, out_iface_id);
  if (status != ZX_OK) {
    IWL_ERR(this, "%s() failed phy create: %s\n", __func__, zx_status_get_string(status));
    return status;
  }

  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans_);
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[*out_iface_id];
  mac_device->set_mvmvif(mvmvif);

  if ((status = mac_device->DdkAdd("iwlwifi-wlanmac", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    IWL_ERR(this, "%s() failed mac device add: %s\n", __func__, zx_status_get_string(status));
    phy_create_iface_undo(iwl_trans_, *out_iface_id);
    return status;
  }

  status = phy_start_iface(iwl_trans_, mac_device->zxdev(), *out_iface_id);
  if (status != ZX_OK) {
    // Freeing of resources allocated in phy_create_iface() will happen via DdkAsynremove().
    IWL_ERR(this, "%s() failed phy start: %s\n", __func__, zx_status_get_string(status));
    goto fail_post_add;
  }

  mac_device->DdkMakeVisible();
  mac_device.release();

  return ZX_OK;

fail_post_add:
  // Lifecycle is managed by the devhost post DdkAdd.
  // Therefore, we need to rely on async remove to free the memory.
  mac_device.release()->DdkAsyncRemove();
  return status;
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  return phy_destroy_iface(iwl_trans_, iface_id);
}

zx_status_t Device::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  return phy_set_country(iwl_trans_, country);
}

zx_status_t Device::WlanphyImplClearCountry() {
  IWL_ERR(this, "%s() not implemented ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::WlanphyImplGetCountry(wlanphy_country_t* out_country) {
  return phy_get_country(iwl_trans_, out_country);
}

}  // namespace wlan::iwlwifi
