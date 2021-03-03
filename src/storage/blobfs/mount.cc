// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/mount.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <utility>

#include "src/storage/blobfs/runner.h"

namespace blobfs {

zx_status_t Mount(std::unique_ptr<BlockDevice> device, const MountOptions& options,
                  fidl::ServerEnd<fuchsia_io::Directory> root, ServeLayout layout,
                  zx::resource vmex_resource) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  auto runner_or = Runner::Create(&loop, std::move(device), options, std::move(vmex_resource));
  if (runner_or.is_error())
    return runner_or.error_value();

  if (zx_status_t status = runner_or.value()->ServeRoot(std::move(root), layout); status != ZX_OK) {
    return status;
  }
  loop.Run();
  return ZX_OK;
}

}  // namespace blobfs
