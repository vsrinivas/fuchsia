// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_UTIL_H_

#include <fuchsia/net/tun/cpp/fidl.h>

namespace network {
namespace tun {

// Helper functions that consolidate FIDL bindings table types by setting default values for fields
// that are not set. Return `false` if a required field is missing or is set and has an invalid
// value.
bool TryConsolidateBaseConfig(fuchsia::net::tun::BaseConfig* config);
bool TryConsolidateDeviceConfig(fuchsia::net::tun::DeviceConfig* config);
bool TryConsolidateDevicePairConfig(fuchsia::net::tun::DevicePairConfig* config);

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_UTIL_H_
