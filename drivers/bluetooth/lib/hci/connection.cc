// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

Connection::LowEnergyParameters::LowEnergyParameters(uint16_t interval_min, uint16_t interval_max,
                                                     uint16_t interval, uint16_t latency,
                                                     uint16_t supervision_timeout)
    : interval_min_(interval_min),
      interval_max_(interval_max),
      interval_(interval),
      latency_(latency),
      supervision_timeout_(supervision_timeout) {
  FTL_DCHECK(interval_min_ <= interval_max_);
}

Connection::Connection(ConnectionHandle handle, Role role,
                       const common::DeviceAddress& peer_address, const LowEnergyParameters& params)
    : ll_type_(LinkType::kLE),
      handle_(handle),
      role_(role),
      peer_address_(peer_address),
      le_params_(std::make_unique<LowEnergyParameters>(params)) {
  FTL_DCHECK(handle);
  FTL_DCHECK(params.interval());
}

}  // namespace hci
}  // namespace bluetooth
