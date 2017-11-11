// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/att/bearer.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace btlib {

namespace att {
class Database;
class PacketReader;
}  // namespace att

namespace gatt {

// A GATT Server implements the server-role of the ATT protocol over a single
// ATT Bearer. A unique Server instance should exist for each logical link that
// supports GATT.
//
// A Server responds to incoming requests by querying the database that it
// is initialized with. Each Server shares an att::Bearer with a Client.
class Server final {
 public:
  // |database| will be queried by the Server to resolve transactions.
  // |bearer| is the ATT data bearer that this Server operates on.
  Server(fxl::RefPtr<att::Database> database, fxl::RefPtr<att::Bearer> bearer);
  ~Server();

 private:
  // ATT protocol request handlers:
  void OnExchangeMTU(att::Bearer::TransactionId tid,
                     const att::PacketReader& packet);
  void OnReadByGroupType(att::Bearer::TransactionId tid,
                         const att::PacketReader& packet);

  fxl::RefPtr<att::Database> db_;
  fxl::RefPtr<att::Bearer> att_;

  // ATT protocol request handler IDs
  att::Bearer::HandlerId exchange_mtu_id_;
  att::Bearer::HandlerId read_by_group_type_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace gatt
}  // namespace btlib
