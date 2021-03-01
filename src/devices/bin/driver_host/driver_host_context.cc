// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_host_context.h"

#include <stdio.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "composite_device.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

DriverHostContext::~DriverHostContext() {
  while (!dead_devices_.is_empty()) {
    delete dead_devices_.pop_front();
  }
}

zx_status_t DriverHostContext::DeviceConnect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags,
                                             zx::channel c) {
  auto options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);

  fbl::RefPtr<fs::Vnode> target;
  if (!options.flags.node_reference) {
    zx_status_t status = dev->vnode->OpenValidating(options, &target);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (target == nullptr) {
    target = dev->vnode;
  }

  return vfs_.Serve(std::move(target), std::move(c), options);
}
