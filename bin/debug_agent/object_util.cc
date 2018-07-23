// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/object_util.h"

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/object.h>

namespace debug_agent {

namespace {

template <typename ResultObject>
std::vector<ResultObject> GetChildObjects(zx_handle_t parent,
                                          uint32_t child_kind) {
  auto koids = GetChildKoids(parent, child_kind);

  std::vector<ResultObject> result;
  result.reserve(koids.size());

  for (zx_koid_t koid : koids) {
    zx_handle_t handle;
    if (zx_object_get_child(parent, koid, ZX_RIGHT_SAME_RIGHTS, &handle) ==
        ZX_OK)
      result.push_back(ResultObject(handle));
  }
  return result;
}

}  // namespace

zx::thread ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid) {
  zx_handle_t thread_handle = ZX_HANDLE_INVALID;
  if (zx_object_get_child(process, thread_koid, ZX_RIGHT_SAME_RIGHTS,
                          &thread_handle) != ZX_OK)
    return zx::thread();
  return zx::thread(thread_handle);
}

zx_koid_t KoidForProcess(const zx::process& process) {
  return KoidForObject(process.get());
}

zx_koid_t KoidForObject(zx_handle_t object) {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(object, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                         nullptr, nullptr) != ZX_OK)
    return 0;
  return info.koid;
}

std::string NameForObject(zx_handle_t object) {
  char name[ZX_MAX_NAME_LEN];
  if (zx_object_get_property(object, ZX_PROP_NAME, name, sizeof(name)) == ZX_OK)
    return std::string(name);
  return std::string();
}

std::vector<zx_koid_t> GetChildKoids(zx_handle_t parent, uint32_t child_kind) {
  constexpr size_t kNumExtraKoids = 10u;

  size_t actual = 0;
  size_t available = 0;
  std::vector<zx_koid_t> result;

  // This is inherently racy, but we retry once with a bit of slop to try to
  // get a complete list.
  for (int pass = 0; pass < 2; pass++) {
    if (actual < available)
      result.resize(available + kNumExtraKoids);
    zx_status_t status = zx_object_get_info(parent, child_kind, result.data(),
                                            result.size() * sizeof(zx_koid_t),
                                            &actual, &available);
    if (status != ZX_OK || actual == available)
      break;
  }
  result.resize(actual);
  return result;
}

std::vector<zx::job> GetChildJobs(zx_handle_t job) {
  return GetChildObjects<zx::job>(job, ZX_INFO_JOB_CHILDREN);
}

std::vector<zx::process> GetChildProcesses(zx_handle_t job) {
  return GetChildObjects<zx::process>(job, ZX_INFO_JOB_PROCESSES);
}

std::vector<zx::thread> GetChildThreads(zx_handle_t process) {
  return GetChildObjects<zx::thread>(process, ZX_INFO_PROCESS_THREADS);
}

}  // namespace debug_agent
