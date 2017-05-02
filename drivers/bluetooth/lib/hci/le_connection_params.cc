// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "le_connection_params.h"

#include "lib/ftl/logging.h"

#include "defaults.h"

namespace bluetooth {
namespace hci {

LEConnectionParams::LEConnectionParams(LEPeerAddressType peer_address_type,
                                       const common::DeviceAddressBytes& peer_address,
                                       uint16_t conn_interval_min, uint16_t conn_interval_max,
                                       uint16_t conn_interval, uint16_t conn_latency,
                                       uint16_t supervision_timeout)
    : peer_address_type_(peer_address_type),
      peer_address_(peer_address),
      conn_interval_min_(conn_interval_min),
      conn_interval_max_(conn_interval_max),
      conn_interval_(conn_interval),
      conn_latency_(conn_latency),
      supervision_timeout_(supervision_timeout) {
  FTL_DCHECK(conn_interval_min_ <= conn_interval_max_);
}

LEConnectionParams::LEConnectionParams(LEPeerAddressType peer_address_type,
                                       const common::DeviceAddressBytes& peer_address)
    : LEConnectionParams(peer_address_type, peer_address, defaults::kLEConnectionIntervalMin,
                         defaults::kLEConnectionIntervalMax, 0x0000, 0x0000,
                         defaults::kLESupervisionTimeout) {}

}  // namespace hci
}  // namespace bluetooth
