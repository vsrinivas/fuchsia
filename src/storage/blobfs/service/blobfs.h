// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_SERVICE_BLOBFS_H_
#define SRC_STORAGE_BLOBFS_SERVICE_BLOBFS_H_

#include <fidl/fuchsia.blobfs/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/blobfs/blobfs.h"

namespace blobfs {

class BlobfsService : public fidl::WireServer<fuchsia_blobfs::Blobfs>, public fs::Service {
 public:
  BlobfsService(async_dispatcher_t* dispatcher, Blobfs& blobfs);

  void SetCorruptBlobHandler(SetCorruptBlobHandlerRequestView request,
                             SetCorruptBlobHandlerCompleter::Sync& completer) override;

 private:
  Blobfs& blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_SERVICE_BLOBFS_H_
