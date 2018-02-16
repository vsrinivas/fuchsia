// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/ref_ptr.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace btlib {

namespace l2cap {
class Channel;
}  // namespace l2cap

namespace att {
class Database;
}  // namespace att

namespace gatt {

class Server;

namespace internal {

// Represents the GATT data channel between the local adapter and a single
// remote peer. A Connection supports simultaneous GATT client and server
// functionality. An instance of Connection should exist on each ACL logical
// link.
class Connection final {
 public:
  // |peer_id| is the 128-bit UUID that identifies the peer device.
  // |local_db| is the local attribute database that the GATT server will
  // operate on. |att_chan| must correspond to an open L2CAP Attribute channel.
  Connection(const std::string& peer_id,
             fbl::RefPtr<l2cap::Channel> att_chan,
             fxl::RefPtr<att::Database> local_db);
  ~Connection();

  Connection() = default;
  Connection(Connection&&) = default;
  Connection& operator=(Connection&&) = default;

  Server* server() const { return server_.get(); }

 private:
  std::unique_ptr<Server> server_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
