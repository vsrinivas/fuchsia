// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "state.h"

namespace network {
namespace tun {

bool MacState::operator==(const MacState& o) const {
  return mode == o.mode && std::equal(multicast_filters.begin(), multicast_filters.end(),
                                      o.multicast_filters.begin(), o.multicast_filters.end(),
                                      [](const fuchsia_net::wire::MacAddress& left,
                                         const fuchsia_net::wire::MacAddress& right) {
                                        return std::equal(left.octets.begin(), left.octets.end(),
                                                          right.octets.begin(), right.octets.end());
                                      });
}

bool InternalState::operator==(const InternalState& o) const {
  return mac == o.mac && has_session == o.has_session;
}

}  // namespace tun
}  // namespace network
