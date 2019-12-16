// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_platform_handle.h"

#include <errno.h>

namespace magma {

// static
bool PlatformHandle::duplicate_handle(uint32_t handle_in, uint32_t* handle_out) {
  int fd = dup(handle_in);
  if (fd < 0)
    return DRETF(false, "dup failed: %s", strerror(errno));
  *handle_out = fd;
  return true;
}

bool PlatformHandle::SupportsGetCount() { return false; }

std::unique_ptr<PlatformHandle> PlatformHandle::Create(uint32_t handle) {
  return std::make_unique<LinuxPlatformHandle>(handle);
}

}  // namespace magma
