// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

AdminService::AdminService(async_dispatcher_t* dispatcher, F2fs* f2fs)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_fs::Admin> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      f2fs_(f2fs) {}

void AdminService::Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) {
  f2fs_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "filesystem shutdown failed: " << zx_status_get_string(status);
    }
    completer.Reply();
  });
}

void AdminService::GetRoot(GetRootRequestView request, GetRootCompleter::Sync& completer) {
  // TODO: Implement GetRoot admin service
}

}  // namespace f2fs
