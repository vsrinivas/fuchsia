// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_handle.h"

#include <lib/zx/handle.h>

#include "platform_object.h"
#include "zircon_platform_port.h"

namespace magma {

bool ZirconPlatformHandle::GetCount(uint32_t* count_out) {
  zx_info_handle_count_t info;
  zx_status_t status =
      zx_object_get_info(get(), ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK)
    return DRETF(false, "zx_object_get_info failed");

  *count_out = info.handle_count;
  return true;
}

uint64_t ZirconPlatformHandle::GetId() {
  uint64_t key;
  PlatformObject::IdFromHandle(get(), &key);
  return key;
}

bool ZirconPlatformHandle::WaitAsync(PlatformPort* port, uint64_t* key_out) {
  if (!PlatformObject::IdFromHandle(get(), key_out))
    return DRET_MSG(false, "IdFromHandle failed");

  auto zircon_port = static_cast<ZirconPlatformPort*>(port);
  zx_status_t status = handle_.wait_async(zircon_port->zx_port(), *key_out,
                                          ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
  if (status != ZX_OK)
    return DRETF(false, "wait_async failed: %d", status);

  return true;
}

std::string ZirconPlatformHandle::GetName() {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status = handle_.get_property(ZX_PROP_NAME, &name, sizeof(name));
  if (status != ZX_OK)
    return "";
  return std::string(name);
}

// static
bool PlatformHandle::duplicate_handle(uint32_t handle_in, uint32_t* handle_out) {
  zx_status_t status = zx_handle_duplicate(handle_in, ZX_RIGHT_SAME_RIGHTS, handle_out);
  if (status != ZX_OK)
    return DRETF(false, "zx_handle_duplicate failed: %d", status);
  return true;
}

bool PlatformHandle::SupportsGetCount() { return true; }

std::unique_ptr<PlatformHandle> PlatformHandle::Create(uint32_t handle) {
  return std::make_unique<ZirconPlatformHandle>(zx::handle(handle));
}

}  // namespace magma
