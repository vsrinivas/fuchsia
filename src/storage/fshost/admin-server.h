// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_ADMIN_SERVER_H_
#define SRC_STORAGE_FSHOST_ADMIN_SERVER_H_

#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/async-loop/default.h>

#include <thread>

#include "fs-manager.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace devmgr {

class FsManager;

class AdminServer final : public fuchsia_fshost::Admin::Interface {
 public:
  AdminServer(devmgr::FsManager* fs_manager) : fs_manager_(fs_manager) {}

  // Creates a new fs::Service backed by a new AdminServer, to be inserted into
  // a pseudo fs.
  static fbl::RefPtr<fs::Service> Create(devmgr::FsManager* fs_manager,
                                         async_dispatcher* dispatcher);

  // Implementation of the Shutdown method from the FIDL protocol.
  void Shutdown(ShutdownCompleter::Sync& completer) override;

 private:
  devmgr::FsManager* fs_manager_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_ADMIN_SERVER_H_
