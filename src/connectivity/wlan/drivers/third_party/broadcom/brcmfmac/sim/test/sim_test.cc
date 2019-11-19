// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

intptr_t SimTest::instance_num_ = 0;

SimTest::SimTest() {
  env_ = std::make_shared<simulation::Environment>();
  env_->AddStation(this);

  dev_mgr_ = std::make_shared<simulation::FakeDevMgr>();
  parent_dev_ = reinterpret_cast<zx_device_t*>(instance_num_++);
}

zx_status_t SimTest::Init() {
  return brcmfmac::SimDevice::Create(parent_dev_, dev_mgr_, env_, &device_);
}

zx_status_t SimTest::CreateInterface(wlan_info_mac_role_t role,
                                     const wlanif_impl_ifc_protocol& sme_protocol,
                                     std::unique_ptr<SimInterface>* ifc_out) {
  zx_status_t status;
  std::unique_ptr<SimInterface> sim_ifc = std::make_unique<SimInterface>();
  if ((status = sim_ifc->Init()) != ZX_OK) {
    return status;
  }

  wlanphy_impl_create_iface_req_t req = {.role = role, .sme_channel = sim_ifc->ch_mlme_};

  if ((status = device_->WlanphyImplCreateIface(&req, &sim_ifc->iface_id_)) != ZX_OK) {
    return status;
  }

  // This should have created a WLANIF_IMPL device
  auto device_info = dev_mgr_->FindFirstByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  if (device_info == std::nullopt) {
    return ZX_ERR_INTERNAL;
  }

  sim_ifc->if_impl_ctx_ = device_info->dev_args.ctx;
  sim_ifc->if_impl_ops_ = static_cast<wlanif_impl_protocol_ops_t*>(device_info->dev_args.proto_ops);

  zx_handle_t sme_ch;
  status = sim_ifc->if_impl_ops_->start(sim_ifc->if_impl_ctx_, &sme_protocol, &sme_ch);

  // Verify that the channel passed back from start() is the same one we gave to create_iface()
  if (sme_ch != sim_ifc->ch_mlme_) {
    return ZX_ERR_INTERNAL;
  }

  *ifc_out = std::move(sim_ifc);
  return ZX_OK;
}

}  // namespace wlan::brcmfmac
