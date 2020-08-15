// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <utility>

#include <factoryfs/mount.h>
#include <factoryfs/query.h>
#include <factoryfs/runner.h>

namespace factoryfs {

zx_status_t Mount(std::unique_ptr<BlockDevice> device, MountOptions* options, zx::channel root,
                  ServeLayout layout) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  std::unique_ptr<Runner> runner;
  zx_status_t status = Runner::Create(&loop, std::move(device), options, &runner);
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

}  // namespace factoryfs
