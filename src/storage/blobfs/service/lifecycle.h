// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_SERVICE_LIFECYCLE_H_
#define SRC_STORAGE_BLOBFS_SERVICE_LIFECYCLE_H_

#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/default.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace blobfs {

class LifecycleServer final : public fidl::WireServer<fuchsia_process_lifecycle::Lifecycle> {
 public:
  using ShutdownCallback = fit::callback<void(fs::FuchsiaVfs::ShutdownCallback)>;

  explicit LifecycleServer(ShutdownCallback shutdown) : shutdown_(std::move(shutdown)) {}

  static void Create(async_dispatcher_t* dispatcher, ShutdownCallback shutdown,
                     fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> request);

  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;

 private:
  ShutdownCallback shutdown_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_SERVICE_LIFECYCLE_H_
