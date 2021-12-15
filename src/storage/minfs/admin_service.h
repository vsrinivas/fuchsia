// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_ADMIN_SERVICE_H_
#define SRC_STORAGE_MINFS_ADMIN_SERVICE_H_

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {

class AdminService : public fidl::WireServer<fuchsia_fs::Admin>, public fs::Service {
 public:
  AdminService(async_dispatcher_t* dispatcher, Minfs& minfs);

  void Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) override;
  void GetRoot(GetRootRequestView request, GetRootCompleter::Sync& completer) override {
    // Not yet supported.
  }

 private:
  Minfs& minfs_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_ADMIN_SERVICE_H_
