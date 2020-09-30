// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "admin-server.h"

#include <lib/fidl-async/cpp/bind.h>

#include <fs/service.h>

namespace devmgr {

fbl::RefPtr<fs::Service> AdminServer::Create(devmgr::FsManager* fs_manager,
                                             async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, fs_manager](zx::channel chan) mutable {
    zx::event event;
    zx_status_t status = fs_manager->event()->duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
    if (status != ZX_OK) {
      fprintf(stderr, "fshost: failed to duplicate event handle for admin service: %s\n",
              zx_status_get_string(status));
      return status;
    }

    status = fidl::BindSingleInFlightOnly(dispatcher, std::move(chan),
                                          std::make_unique<AdminServer>(fs_manager));
    if (status != ZX_OK) {
      fprintf(stderr, "fshost: failed to bind admin service: %s\n", zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  });
}

void AdminServer::Shutdown(ShutdownCompleter::Sync& completer) {
  printf("fshost: received shutdown command over admin interface\n");
  fs_manager_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      printf("fshost: error waiting for FSHOST_SIGNAL_EXIT_DONE: %s\n",
             zx_status_get_string(status));
    } else {
      completer.Reply();
      printf("fshost: shutdown complete\n");
    }
  });
}

}  // namespace devmgr
