// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lifecycle.h"

#include <lib/fidl-async/cpp/bind.h>

namespace devmgr {

zx_status_t LifecycleServer::Create(async_dispatcher_t* dispatcher, devmgr::FsManager* fs_manager,
                                    zx::channel chan) {
  zx_status_t status = fidl::BindSingleInFlightOnly(dispatcher, std::move(chan),
                                                    std::make_unique<LifecycleServer>(fs_manager));
  if (status != ZX_OK) {
    fprintf(stderr, "fshost: failed to bind lifecycle service: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void LifecycleServer::Stop(StopCompleter::Sync& completer) {
  printf("fshost: received shutdown command over lifecycle interface\n");
  fs_manager_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      printf("fshost: error waiting for FSHOST_SIGNAL_EXIT_DONE: %s\n",
             zx_status_get_string(status));
    } else {
      printf("fshost: shutdown complete\n");
    }
    completer.Close(status);
    // TODO(sdemos): this should send a signal to the main thread to exit
  });
}

}  // namespace devmgr
