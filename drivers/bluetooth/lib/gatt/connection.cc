// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include "garnet/drivers/bluetooth/lib/att/database.h"

#include "client.h"
#include "server.h"

namespace btlib {
namespace gatt {
namespace internal {

Connection::Connection(const std::string& peer_id,
                       fxl::RefPtr<att::Bearer> att_bearer,
                       fxl::RefPtr<att::Database> local_db,
                       RemoteServiceWatcher svc_watcher,
                       async_dispatcher_t* gatt_dispatcher)
    : att_(att_bearer) {
  FXL_DCHECK(att_bearer);
  FXL_DCHECK(local_db);
  FXL_DCHECK(svc_watcher);
  FXL_DCHECK(gatt_dispatcher);

  server_ = std::make_unique<gatt::Server>(peer_id, local_db, att_);
  remote_service_manager_ = std::make_unique<RemoteServiceManager>(
      gatt::Client::Create(att_), gatt_dispatcher);
  remote_service_manager_->set_service_watcher(std::move(svc_watcher));
}

void Connection::Initialize() {
  FXL_DCHECK(remote_service_manager_);
  remote_service_manager_->Initialize([att = att_](att::Status status) {
    if (status) {
      FXL_VLOG(1) << "gatt: Primary service discovery complete";
    } else {
      FXL_VLOG(1) << "gatt: Client setup failed - " << status.ToString();

      // Signal a link error.
      att->ShutDown();
    }
  });
}

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
