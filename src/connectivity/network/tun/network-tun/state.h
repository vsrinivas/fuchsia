// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_STATE_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_STATE_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fidl/fuchsia.net/cpp/wire.h>

namespace network {
namespace tun {

class MacState {
 public:
  bool operator==(const MacState&) const;

  fuchsia_hardware_network::wire::MacFilterMode mode;
  std::vector<fuchsia_net::wire::MacAddress> multicast_filters;
};

class InternalState {
 public:
  bool operator==(const InternalState&) const;

  std::optional<MacState> mac;
  bool has_session;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_STATE_H_
