// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <unordered_map>

#include <ddk/device.h>
#include <wlan/common/dispatcher.h>

namespace wlan {
namespace testing {

class IfaceDevice;

class DeviceConnector;

class PhyDevice : public ::fuchsia::wlan::device::Phy {
 public:
  PhyDevice(zx_device_t* device);
  virtual ~PhyDevice() = default;

  zx_status_t Bind();

  void Unbind();
  void Release();
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  virtual void Query(QueryCallback callback) override;
  virtual void CreateIface(::fuchsia::wlan::device::CreateIfaceRequest req,
                           CreateIfaceCallback callback) override;
  virtual void DestroyIface(::fuchsia::wlan::device::DestroyIfaceRequest req,
                            DestroyIfaceCallback callback) override;
  virtual void SetCountry(::fuchsia::wlan::device::CountryCode req,
                          SetCountryCallback callback) override;
  virtual void GetCountry(GetCountryCallback callback) override;
  virtual void ClearCountry(ClearCountryCallback callback) override;

 private:
  zx_status_t Connect(zx::channel request);

  zx_device_t* zxdev_;
  zx_device_t* parent_;

  std::mutex lock_;
  std::unique_ptr<wlan::common::Dispatcher<::fuchsia::wlan::device::Phy>> dispatcher_;
  std::unordered_map<uint16_t, IfaceDevice*> ifaces_;
  // Next available Iface id. Must be checked against the map to prevent overwriting an existing
  // IfaceDevice pointer in the map.
  uint16_t next_id_ = 0;

  friend class DeviceConnector;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_PHY_DEVICE_H_
