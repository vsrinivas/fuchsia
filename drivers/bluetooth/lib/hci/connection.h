// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/connection_parameters.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace bluetooth {
namespace hci {

class Transport;

// Represents a logical link connection to a remote device. This class is not
// thread-safe. Instances should only be accessed on their creation thread.
class Connection final {
 public:
  // This defines the various connection types. These do not exactly correspond
  // to the baseband logical/physical link types but instead provide a
  // high-level abstraction.
  enum class LinkType {
    // Represents a BR/EDR baseband link (ACL-U).
    kACL,

    // BR/EDR isochronous links (SCO-S, eSCO-S).
    kSCO,
    kESCO,

    // A LE logical link (LE-U).
    kLE,
  };

  // Role of the local device in the established connection.
  enum class Role {
    kMaster,
    kSlave,
  };

  // Initializes this as a LE ACL connection.
  Connection(ConnectionHandle handle,
             Role role,
             const common::DeviceAddress& peer_address,
             const LEConnectionParameters& params,
             fxl::RefPtr<Transport> hci);

  // The destructor closes this connection.
  ~Connection();

  // The type of the connection.
  LinkType ll_type() const { return ll_type_; }

  // Returns the 12-bit connection handle of this connection. This handle is
  // used to identify an individual logical link maintained by the controller.
  ConnectionHandle handle() const { return handle_; }

  // Returns the role of the local device in the established connection.
  Role role() const { return role_; }

  // The active LE connection parameters of this connection. Must only be called
  // on a Connection with the LE link type.
  const LEConnectionParameters& low_energy_parameters() const {
    FXL_DCHECK(ll_type_ == LinkType::kLE);
    return le_params_;
  }

  // Sets the active LE parameters of this connection. Must only be called on a
  // Connection with the LE link type.
  void set_low_energy_parameters(const LEConnectionParameters& params) {
    FXL_DCHECK(ll_type_ == LinkType::kLE);
    le_params_ = params;
  }

  // The identity address of the peer device.
  // TODO(armansito): Implement mechanism to store identity address here after
  // address resolution.
  const common::DeviceAddress& peer_address() const { return peer_address_; }

  // Returns true if this connection is currently open.
  bool is_open() const { return is_open_; }
  void set_closed() { is_open_ = false; }

  // Closes this connection by sending the HCI_Disconnect command to the
  // controller. This method is a NOP if the connecton is already closed.
  void Close(Status reason = Status::kRemoteUserTerminatedConnection);

  std::string ToString() const;

 private:
  LinkType ll_type_;
  ConnectionHandle handle_;
  Role role_;
  bool is_open_;

  fxl::ThreadChecker thread_checker_;

  // The address of the peer device.
  common::DeviceAddress peer_address_;

  // Connection parameters for a LE link. Not nullptr if the link type is LE.
  LEConnectionParameters le_params_;

  // The underlying HCI transport. We use this to terminate the connection by
  // sending the HCI_Disconnect command.
  fxl::RefPtr<Transport> hci_;

  // TODO(armansito): Add a BREDRParameters struct.

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<Connection> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace hci
}  // namespace bluetooth
