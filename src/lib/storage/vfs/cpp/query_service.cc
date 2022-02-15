// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/query_service.h"

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/fidl-async/cpp/bind.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs {

QueryService::QueryService(FuchsiaVfs* vfs)
    : fs::Service(
          [dispatcher = vfs->dispatcher(), this](fidl::ServerEnd<fuchsia_fs::Query> server_end) {
            return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
          }),
      vfs_(vfs) {}

void QueryService::IsNodeInFilesystem(IsNodeInFilesystemRequestView request,
                                      IsNodeInFilesystemCompleter::Sync& completer) {
  bool result = false;
  if (vfs_)
    result = vfs_->IsTokenAssociatedWithVnode(std::move(request->token));
  completer.Reply(result);
}

}  // namespace fs
