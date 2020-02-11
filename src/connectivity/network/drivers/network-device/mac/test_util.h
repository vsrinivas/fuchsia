// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_TEST_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_TEST_UTIL_H_

#include <lib/zx/event.h>

#include "mac_interface.h"

namespace network {
namespace testing {

constexpr zx_signals_t kConfigurationChangedEvent = ZX_USER_SIGNAL_0;

class FakeMacDeviceImpl : public ddk::MacAddrImplProtocol<FakeMacDeviceImpl> {
 public:
  FakeMacDeviceImpl();

  zx_status_t CreateChild(std::unique_ptr<MacAddrDevice>* out);

  void MacAddrImplGetAddress(uint8_t out_mac[MAC_SIZE]);
  void MacAddrImplGetFeatures(features_t* out_features);

  void MacAddrImplSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                          size_t multicast_macs_count);

  zx_status_t WaitConfigurationChanged();

  uint8_t* mac() { return mac_; }

  features_t& features() { return features_; }

  mode_t mode() { return mode_; }

  std::vector<MacAddress>& addresses() { return addresses_; }

  mac_addr_impl_protocol_t proto() {
    return mac_addr_impl_protocol_t{.ops = &mac_addr_impl_protocol_ops_, .ctx = this};
  }

 private:
  uint8_t mac_[MAC_SIZE] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06};
  features_t features_{};
  mode_t mode_ = 0;
  std::vector<MacAddress> addresses_;
  zx::event event_;
};
}  // namespace testing
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_MAC_TEST_UTIL_H_
