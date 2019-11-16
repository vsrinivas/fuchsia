// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_

#include <zircon/types.h>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"

namespace wlan::brcmfmac {

// This class represents an interface created on a simulated device, collecting all of the
// attributes related to that interface.
struct SimInterface {
  SimInterface() = default;

  zx_status_t Init() { return zx_channel_create(0, &ch_sme_, &ch_mlme_); }

  ~SimInterface() {
    if (ch_sme_ != ZX_HANDLE_INVALID) {
      zx_handle_close(ch_sme_);
    }
    if (ch_mlme_ != ZX_HANDLE_INVALID) {
      zx_handle_close(ch_mlme_);
    }
  }

  // This provides our DDK (wlanif-impl) API into the interface
  device_add_args_t if_impl_args_;

  // Unique identifier provided by the driver
  uint16_t iface_id_;

  // Handles for SME <=> MLME communication, required but never used for communication (since no
  // SME is present).
  zx_handle_t ch_sme_ = ZX_HANDLE_INVALID;   // SME-owned side
  zx_handle_t ch_mlme_ = ZX_HANDLE_INVALID;  // MLME-owned side
};

// A base class that can be used for creating simulation tests. It provides functionality that
// should be common to most tests (like creating a new device instance and setting up and plugging
// into the environment). It also provides a factory method for creating a new interface on the
// simulated device.
class SimTest : public ::testing::Test, public simulation::StationIfc {
 public:
  SimTest();
  zx_status_t Init();

  std::shared_ptr<simulation::Environment> env_;

  static intptr_t instance_num_;

 protected:
  // Create a new interface on the simulated device, providing the specified role and function
  // callbacks
  zx_status_t CreateInterface(wlan_info_mac_role_t role,
                              const wlanif_impl_ifc_protocol& sme_protocol,
                              std::unique_ptr<SimInterface>* ifc_out);

  // Fake device manager
  std::shared_ptr<simulation::FakeDevMgr> dev_mgr_;

  // brcmfmac's concept of a device
  std::unique_ptr<brcmfmac::SimDevice> device_;

 private:
  // StationIfc methods - by default, do nothing. These can/will be overridden by superclasses.
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {}
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {}
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override {}
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override {}
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {}
  void ReceiveNotification(void* payload) override {}

  // Contrived pointer used as a stand-in for the (opaque) parent device
  zx_device_t* parent_dev_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_SIM_TEST_H_
