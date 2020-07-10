// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <utility>

#include <blobfs/mount.h>

#include "runner.h"

namespace blobfs {

zx_status_t Mount(std::unique_ptr<BlockDevice> device, MountOptions* options, zx::channel root,
                  ServeLayout layout, zx::resource vmex_resource, zx::channel diagnostics_dir) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  std::unique_ptr<Runner> runner;
  zx_status_t status = Runner::Create(&loop, std::move(device), options, std::move(vmex_resource),
                                      std::move(diagnostics_dir), &runner);
  if (status != ZX_OK) {
    return status;
  }

  status = runner->ServeRoot(std::move(root), layout);
  if (status != ZX_OK) {
    return status;
  }
  loop.Run();
  return ZX_OK;
}

}  // namespace blobfs
