// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/gatt/server.h"

namespace btlib {
namespace gatt {

Connection::Connection(const std::string& peer_id,
                       std::unique_ptr<l2cap::Channel> att_chan,
                       fxl::RefPtr<att::Database> local_db) {
  auto att = att::Bearer::Create(std::move(att_chan));
  FXL_DCHECK(att);

  server_ = std::make_unique<gatt::Server>(peer_id, local_db, std::move(att));
}

Connection::~Connection() {}

}  // namespace gatt
}  // namespace btlib
