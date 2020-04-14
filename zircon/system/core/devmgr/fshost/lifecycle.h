// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_LIFECYCLE_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_LIFECYCLE_H_

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

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_FSHOST_LIFECYCLE_H_
