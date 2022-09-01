// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <lib/ddk/device.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <unordered_map>

#include <wlan/common/dispatcher.h>

namespace wlan {
namespace testing {

class IfaceDevice;

class DeviceConnector;

class PhyDevice : public fidl::WireServer<fuchsia_wlan_device::Phy> {
 public:
  explicit PhyDevice(zx_device_t* device);
  ~PhyDevice() override = default;

  zx_status_t Bind();

  void Unbind();
  void Release();
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  void GetSupportedMacRoles(GetSupportedMacRolesCompleter::Sync& completer) override;
  void CreateIface(CreateIfaceRequestView req, CreateIfaceCompleter::Sync& completer) override;
  void DestroyIface(DestroyIfaceRequestView req, DestroyIfaceCompleter::Sync& completer) override;
  void SetCountry(SetCountryRequestView req, SetCountryCompleter::Sync& completer) override;
  void GetCountry(GetCountryCompleter::Sync& completer) override;
  void ClearCountry(ClearCountryCompleter::Sync& completer) override;
  void SetPsMode(SetPsModeRequestView req, SetPsModeCompleter::Sync& completer) override;
  void GetPsMode(GetPsModeCompleter::Sync& completer) override;

 private:
  zx_status_t Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end);

  zx_device_t* zxdev_;
  zx_device_t* parent_;

  std::mutex lock_;
  std::unique_ptr<wlan::common::Dispatcher<fuchsia_wlan_device::Phy>> dispatcher_;
  std::unordered_map<uint16_t, IfaceDevice*> ifaces_;
  // Next available Iface id. Must be checked against the map to prevent overwriting an existing
  // IfaceDevice pointer in the map.
  uint16_t next_id_ = 0;

  friend class DeviceConnector;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_
