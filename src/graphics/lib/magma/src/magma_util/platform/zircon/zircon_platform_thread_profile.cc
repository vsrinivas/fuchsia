// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <magma_util/macros.h>

#include "platform_thread.h"

namespace magma {

bool PlatformThreadHelper::SetRole(void* device_handle, const std::string& role_name) {
  if (device_handle == nullptr)
    return DRETF(false, "Device handle is nullptr");

  zx_device_t* device = static_cast<zx_device_t*>(device_handle);
  const zx_handle_t thread_handle = thrd_get_zx_handle(thrd_current());

  const zx_status_t status =
      device_set_profile_by_role(device, thread_handle, role_name.data(), role_name.size());
  if (status != ZX_OK)
    return DRETF(false, "Failed to set role \"%s\": %d", role_name.c_str(), status);

  return true;
}

bool PlatformThreadHelper::SetThreadRole(void* device_handle, thrd_t thread,
                                         const std::string& role_name) {
  if (device_handle == nullptr)
    return DRETF(false, "Device handle is nullptr");

  const zx_handle_t thread_handle = thrd_get_zx_handle(thread);
  if (thread_handle == ZX_HANDLE_INVALID)
    return DRETF(false, "Invalid thread handle");

  zx_device_t* device = static_cast<zx_device_t*>(device_handle);

  const zx_status_t status =
      device_set_profile_by_role(device, thread_handle, role_name.data(), role_name.size());
  if (status != ZX_OK)
    return DRETF(false, "Failed to set role \"%s\": %d", role_name.c_str(), status);

  return true;
}

}  // namespace magma
