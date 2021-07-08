// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "admin-server.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/service.h"

namespace fshost {

fbl::RefPtr<fs::Service> AdminServer::Create(FsManager* fs_manager,
                                             async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, fs_manager](fidl::ServerEnd<fuchsia_fshost::Admin> chan) {
        zx_status_t status = fidl::BindSingleInFlightOnly(
            dispatcher, std::move(chan), std::make_unique<AdminServer>(fs_manager));
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "failed to bind admin service: " << zx_status_get_string(status);
          return status;
        }
        return ZX_OK;
      });
}

void AdminServer::Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) {
  FX_LOGS(INFO) << "received shutdown command over admin interface";
  fs_manager_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "filesystem shutdown failed: " << zx_status_get_string(status);
    } else {
      completer.Reply();
      FX_LOGS(INFO) << "shutdown complete";
    }
  });
}

}  // namespace fshost
