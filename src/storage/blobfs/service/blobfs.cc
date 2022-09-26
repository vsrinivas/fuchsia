// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/service/blobfs.h"

#include <fidl/fuchsia.blobfs/cpp/wire.h>
#include <lib/fidl-async/cpp/bind.h>

#include "src/lib/storage/vfs/cpp/service.h"

namespace blobfs {

BlobfsService::BlobfsService(async_dispatcher_t* dispatcher, Blobfs& blobfs)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_blobfs::Blobfs> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      blobfs_(blobfs) {}

void BlobfsService::SetCorruptBlobHandler(SetCorruptBlobHandlerRequestView request,
                                          SetCorruptBlobHandlerCompleter::Sync& completer) {
  blobfs_.SetCorruptBlobHandler(std::move(request->handler));
  completer.Reply(ZX_OK);
}

}  // namespace blobfs
