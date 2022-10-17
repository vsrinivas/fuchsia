// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_util.h"

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace network {
namespace testing {

FakeMacDeviceImpl::FakeMacDeviceImpl() {
  // setup default info
  features_.multicast_filter_count = MAX_MAC_FILTER / 2;
  features_.supported_modes = kSupportedModesMask;

  EXPECT_OK(zx::event::create(0, &event_));
}

zx::result<std::unique_ptr<MacAddrDeviceInterface>> FakeMacDeviceImpl::CreateChild() {
  auto protocol = proto();
  return MacAddrDeviceInterface::Create(ddk::MacAddrProtocolClient(&protocol));
}

void FakeMacDeviceImpl::MacAddrGetAddress(uint8_t* out_mac) {
  std::copy(mac_.octets.begin(), mac_.octets.end(), out_mac);
}

void FakeMacDeviceImpl::MacAddrGetFeatures(features_t* out_features) { *out_features = features_; }

void FakeMacDeviceImpl::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                       size_t multicast_macs_count) {
  EXPECT_EQ(mode & kSupportedModesMask, mode);
  EXPECT_NE(mode, 0u);
  mode_t old_mode = mode_;
  mode_ = mode;
  addresses_.clear();
  for (size_t i = 0; i < multicast_macs_count; i++) {
    MacAddress mac{};
    memcpy(mac.octets.data(), multicast_macs_list, MAC_SIZE);
    multicast_macs_list += MAC_SIZE;
    addresses_.push_back(mac);
  }
  // Signal only if this wasn't the first time, given we always get a SetMode on startup.
  if (old_mode != 0) {
    event_.signal(0, kConfigurationChangedEvent);
  }
}

zx_status_t FakeMacDeviceImpl::WaitConfigurationChanged() {
  zx_status_t status = event_.wait_one(kConfigurationChangedEvent, zx::time::infinite(), nullptr);
  event_.signal(kConfigurationChangedEvent, 0);
  return status;
}

}  // namespace testing
}  // namespace network
