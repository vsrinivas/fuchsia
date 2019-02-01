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

std::pair<zx_koid_t, zx_koid_t> GetKoids(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return std::pair<zx_koid_t, zx_koid_t>(
      status == ZX_OK ? info.koid : ZX_KOID_INVALID,
      status == ZX_OK ? info.related_koid : ZX_KOID_INVALID);
}

FXL_EXPORT zx_obj_type_t GetType(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.type : ZX_OBJ_TYPE_NONE;
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

zx_koid_t GetCurrentProcessKoid() { return GetKoid(zx_process_self()); }

std::string GetCurrentProcessName() { return GetObjectName(zx_process_self()); }

zx_koid_t GetCurrentThreadKoid() { return GetKoid(zx_thread_self()); }

std::string GetCurrentThreadName() { return GetObjectName(zx_thread_self()); }

zx_status_t SetCurrentThreadName(const std::string& name) {
  return SetObjectName(zx_thread_self(), name);
}

zx::duration GetCurrentThreadTotalRuntime() {
  zx_info_thread_stats_t info;
  zx_status_t status =
      zx_object_get_info(zx_thread_self(), ZX_INFO_THREAD_STATS, &info,
                         sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? zx::nsec(info.total_runtime) : zx::nsec(0);
}

size_t GetCurrentProcessMemoryMappedBytes() {
  zx_info_task_stats_t info;
  zx_status_t status =
      zx_object_get_info(zx_process_self(), ZX_INFO_TASK_STATS, &info,
                         sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.mem_mapped_bytes : 0;
}

size_t GetCurrentProcessMemoryPrivateBytes() {
  zx_info_task_stats_t info;
  zx_status_t status =
      zx_object_get_info(zx_process_self(), ZX_INFO_TASK_STATS, &info,
                         sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.mem_private_bytes : 0;
}

size_t GetCurrentProcessMemorySharedBytes() {
  zx_info_task_stats_t info;
  zx_status_t status =
      zx_object_get_info(zx_process_self(), ZX_INFO_TASK_STATS, &info,
                         sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.mem_shared_bytes : 0;
}

size_t GetCurrentProcessMemoryScaledSharedBytes() {
  zx_info_task_stats_t info;
  zx_status_t status =
      zx_object_get_info(zx_process_self(), ZX_INFO_TASK_STATS, &info,
                         sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.mem_scaled_shared_bytes : 0;
}

}  // namespace fsl
