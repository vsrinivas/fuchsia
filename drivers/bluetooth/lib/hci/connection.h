// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/hci/hci.h"

namespace bluetooth {
namespace hci {

// Represents a logical link connection to a remote device.
class Connection final {
 public:
  // This defines the various connection types. These do not exactly correspond to the baseband
  // logical/physical link types but instead provide a high-level abstraction.
  enum class LinkType {
    // Represents a BR/EDR baseband link. While LE-U logical links are also considered ACL links, we
    // keep separate declarations here.
    kACL,

    // BR/EDR isochronous links.
    kSCO,
    kESCO,

    // An LE logical link.
    kLE,
  };

  // Role of the local device in the established connection.
  enum class Role {
    kMaster,
    kSlave,
  };

  // Connection parameters for a LE connection.
  class LowEnergyParameters {
   public:
    LowEnergyParameters(uint16_t interval_min, uint16_t interval_max, uint16_t interval,
                        uint16_t latency, uint16_t supervision_timeout);

    // The minimum and maximum allowed connection intervals. The connection interval indicates the
    // frequency of link layer connection events over which data channel PDUs can be transmitted.
    // See Core Spec v5.0, Vol 6, Part B, Section 4.5.1 for more information on the link layer
    // connection events.
    uint16_t interval_min() const { return interval_min_; }
    uint16_t interval_max() const { return interval_max_; }

    // The actual connection interval used for a connection. This parameter is only valid for an
    // active connection and will be set to 0 when an instance of this class is used during a
    // connection request.
    uint16_t interval() const { return interval_; }

    // The maximum allowed connection latency. See Core Spec v5.0, Vol 6, Part B, Section 4.5.2.
    uint16_t latency() const { return latency_; }

    // This defines the maximum time between two received data packet PDUs before the connection is
    // considered lost. See Core Spec v5.0, Vol 6, Part B, Section 4.5.2. This value is given in
    // centiseconds and must be within the range 100 ms - 32 s (10 cs - 3200 cs).
    uint16_t supervision_timeout() const { return supervision_timeout_; }

   private:
    uint16_t interval_min_;
    uint16_t interval_max_;
    uint16_t interval_;
    uint16_t latency_;
    uint16_t supervision_timeout_;
  };

  // Initializes this as a LE connection.
  Connection(ConnectionHandle handle, Role role, const common::DeviceAddress& peer_address,
             const LowEnergyParameters& params);

  // The type of the connection.
  LinkType ll_type() const { return ll_type_; }

  // Returns the 12-bit connection handle of this connection. This handle is used to identify an
  // individual logical link maintained by the controller.
  ConnectionHandle handle() const { return handle_; }

  // Returns the role of the local device in the established connection.
  Role role() const { return role_; }

  // The LE connection parameters of this connection. Must only be called on a Connection with a
  // link type LinkType::kLE.
  const LowEnergyParameters& low_energy_parameters() const {
    FTL_DCHECK(ll_type_ == LinkType::kLE);
    FTL_DCHECK(le_params_);
    return *le_params_;
  }

 private:
  LinkType ll_type_;
  ConnectionHandle handle_;
  Role role_;

  // The address of the peer device.
  common::DeviceAddress peer_address_;

  // Connection parameters for a LE link. Not nullptr if the link type is LE.
  std::unique_ptr<LowEnergyParameters> le_params_;

  // TODO(armansito): Add a BREDRParameters struct.

  FTL_DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace hci
}  // namespace bluetooth
