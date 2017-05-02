// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/hci/hci.h"

namespace bluetooth {
namespace hci {

// Represents the set of connection parameters that are used in a LE logical link.
class LEConnectionParams final {
 public:
  LEConnectionParams(LEPeerAddressType peer_address_type,
                     const common::DeviceAddressBytes& peer_address, uint16_t conn_interval_min,
                     uint16_t conn_interval_max, uint16_t conn_interval, uint16_t conn_latency,
                     uint16_t supervision_timeout);

  // Initializes the connection parameters to the defaults defined in defaults.h.
  // Sets the Connection Latency and Connection Interval parameters to 0x0000.
  //
  // This constructor is useful when initializing connection parameters to be used in a
  // HCI_LE_Create_Connection command.
  LEConnectionParams(LEPeerAddressType peer_address_type,
                     const common::DeviceAddressBytes& peer_address);

  // These return the minimum and maximum allowed connection intervals. The connection interval
  // indicates the frequency of link layer connection events over which data channel PDUs can be
  // transmitted. See Core Spec v5.0, Vol 6, Part B, Section 4.5.1 for more information on the link
  // layer connection events.
  uint16_t connection_interval_min() const { return conn_interval_min_; }
  uint16_t connection_interval_max() const { return conn_interval_max_; }

  // The actual connection interval used for a connection. This parameter is only valid for an
  // active connection and will be set to 0 when an instance of this class is used during a
  // connection request.
  uint16_t connection_interval() const { return conn_interval_; }

  // The maximum allowed connection latency. See Core Spec v5.0, Vol 6, Part B, Section 4.5.2.
  uint16_t connection_latency() const { return conn_latency_; }

  // This defines the maximum time between two received data packet PDUs before the connection is
  // considered lost. See Core Spec v5.0, Vol 6, Part B, Section 4.5.2.
  uint16_t supervision_timeout() const { return supervision_timeout_; }

  // The address type of the peer device.
  LEPeerAddressType peer_address_type() const { return peer_address_type_; }

  // The device address of the peer device.
  const common::DeviceAddressBytes& peer_address() const { return peer_address_; }

 private:
  LEPeerAddressType peer_address_type_;
  common::DeviceAddressBytes peer_address_;
  uint16_t conn_interval_min_;
  uint16_t conn_interval_max_;
  uint16_t conn_interval_;
  uint16_t conn_latency_;
  uint16_t supervision_timeout_;
};

}  // namespace hci
}  // namespace bluetooth
