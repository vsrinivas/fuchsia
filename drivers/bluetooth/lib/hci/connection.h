// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/le_connection_params.h"

namespace bluetooth {
namespace hci {

// Represents a logical link connection to a remote device.
class Connection final : public ftl::RefCountedThreadSafe<Connection> {
 public:
  // This defines the various connection types. These do not exactly correspond to the baseband
  // logical/physical link types but instead provide a high-level abstraction.
  //
  // TODO(armansito): For now we are only declaring BR/EDR & LE types and leaving AMP for another
  // day.
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

  // Initializes a LE connection.
  static ftl::RefPtr<Connection> NewLEConnection(ConnectionHandle handle, Role role,
                                                 const LEConnectionParams& params);

  // The type of the connection.
  LinkType type() const { return type_; }

  // Returns the 12-bit connection handle of this connection. This handle is used to identify an
  // individual logical link maintained by the controller.
  ConnectionHandle handle() const { return handle_; }

  // Returns the role of the local device in the established connection.
  Role role() const { return role_; }

  // The LE connection parameters of this connection if the link type is LE, otherwise returns
  // nullptr.
  const LEConnectionParams* GetLEConnectionParams() const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Connection);
  Connection() = default;
  ~Connection() = default;

  LinkType type_;
  ConnectionHandle handle_;
  Role role_;

  // TODO(armansito): Since we only support LE at the moment we only store LE parameters.
  std::unique_ptr<LEConnectionParams> le_conn_params_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace hci
}  // namespace bluetooth
