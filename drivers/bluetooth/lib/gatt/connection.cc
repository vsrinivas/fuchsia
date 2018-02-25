// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/att/database.h"

#include "client.h"
#include "server.h"

namespace btlib {
namespace gatt {
namespace internal {

Connection::Connection(const std::string& peer_id,
                       fbl::RefPtr<l2cap::Channel> att_chan,
                       fxl::RefPtr<att::Database> local_db) {
  auto att = att::Bearer::Create(std::move(att_chan));
  FXL_DCHECK(att);

  client_ = std::make_unique<gatt::Client>(att);
  server_ = std::make_unique<gatt::Server>(peer_id, local_db, att);

  // Negotiate the MTU right away.
  client_->ExchangeMTU([](att::ErrorCode ecode, uint16_t mtu) {
    // TODO(NET-288): Format this properly using common::Status.
    if (ecode != att::ErrorCode::kNoError) {
      FXL_LOG(ERROR) << "gatt: MTU exchange failed: "
                     << static_cast<unsigned int>(ecode);
    } else {
      FXL_VLOG(1) << "gatt: MTU exchanged: " << mtu;
    }
  });
}

Connection::~Connection() {}

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
