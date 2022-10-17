// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_SERVICE_STARTUP_H_
#define SRC_STORAGE_MINFS_SERVICE_STARTUP_H_

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/resource.h>

#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/minfs/mount.h"

namespace minfs {

using ConfigureCallback = fit::callback<zx::result<>(std::unique_ptr<Bcache>, const MountOptions&)>;

class StartupService final : public fidl::WireServer<fuchsia_fs_startup::Startup>,
                             public fs::Service {
 public:
  StartupService(async_dispatcher_t* dispatcher, ConfigureCallback cb);

  void Start(StartRequestView request, StartCompleter::Sync& completer) final;
  void Format(FormatRequestView request, FormatCompleter::Sync& completer) final;
  void Check(CheckRequestView request, CheckCompleter::Sync& completer) final;

 private:
  ConfigureCallback configure_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_SERVICE_STARTUP_H_
