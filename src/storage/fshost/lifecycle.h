// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_LIFECYCLE_H_
#define SRC_STORAGE_FSHOST_LIFECYCLE_H_

#include <fuchsia/process/lifecycle/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include <fs/service.h>

#include "fs-manager.h"

namespace devmgr {

class FsManager;

class LifecycleServer final : public llcpp::fuchsia::process::lifecycle::Lifecycle::Interface {
 public:
  LifecycleServer(FsManager* fs_manager) : fs_manager_(fs_manager) {}

  static zx_status_t Create(async_dispatcher_t* dispatcher, FsManager* fs_manager,
                            zx::channel chan);

  void Stop(StopCompleter::Sync completer) override;

 private:
  FsManager* fs_manager_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_LIFECYCLE_H_
