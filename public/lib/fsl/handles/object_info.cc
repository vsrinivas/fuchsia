// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/handles/object_info.h"

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>

namespace fsl {

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetRelatedKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.related_koid : ZX_KOID_INVALID;
}

std::string GetObjectName(zx_handle_t handle) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status =
      zx_object_get_property(handle, ZX_PROP_NAME, name, sizeof(name));
  return status == ZX_OK ? std::string(name) : std::string();
}

zx_status_t SetObjectName(zx_handle_t handle, const std::string& name) {
  return zx_object_set_property(handle, ZX_PROP_NAME, name.c_str(),
                                name.size());
}

zx_koid_t GetCurrentProcessKoid() {
  return GetKoid(zx_process_self());
}

std::string GetCurrentProcessName() {
  return GetObjectName(zx_process_self());
}

zx_koid_t GetCurrentThreadKoid() {
  return GetKoid(thrd_get_zx_handle(thrd_current()));
}

std::string GetCurrentThreadName() {
  return GetObjectName(thrd_get_zx_handle(thrd_current()));
}

zx_status_t SetCurrentThreadName(const std::string& name) {
  return SetObjectName(thrd_get_zx_handle(thrd_current()), name);
}

}  // namespace fsl
