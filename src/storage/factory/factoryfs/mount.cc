// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/mount.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <utility>

#include "src/storage/factory/factoryfs/runner.h"

namespace factoryfs {

zx_status_t Mount(std::unique_ptr<BlockDevice> device, MountOptions* options,
                  fidl::ServerEnd<fuchsia_io::Directory> root) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  auto runner_or = Runner::Create(&loop, std::move(device), options);
  if (runner_or.is_error())
    return runner_or.error_value();

  if (zx_status_t status = runner_or->ServeRoot(std::move(root)); status != ZX_OK)
    return status;

  loop.Run();
  return ZX_OK;
}

}  // namespace factoryfs
