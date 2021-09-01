// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_LIFECYCLE_H_
#define SRC_STORAGE_FSHOST_LIFECYCLE_H_

#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/fshost/fs-manager.h"

namespace fshost {

class FsManager;

class LifecycleServer final : public fidl::WireServer<fuchsia_process_lifecycle::Lifecycle> {
 public:
  explicit LifecycleServer(FsManager* fs_manager) : fs_manager_(fs_manager) {}

  static zx_status_t Create(async_dispatcher_t* dispatcher, FsManager* fs_manager,
                            fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> chan);

  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;

 private:
  FsManager* fs_manager_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_LIFECYCLE_H_
