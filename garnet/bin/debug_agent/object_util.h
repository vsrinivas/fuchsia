// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include <lib/zx/object.h>
#include <zircon/types.h>

namespace zx {
class job;
class process;
class thread;
}  // namespace zx

namespace debug_agent {

zx::thread ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid);

zx_koid_t KoidForObject(zx_handle_t object);
inline zx_koid_t KoidForObject(const zx::object_base& object) {
  return KoidForObject(object.get());
}

// Returns the empty string on failure. The empty string might also be a valid
// name, so this is intended for cases where failure isn't critical to detect.
std::string NameForObject(zx_handle_t object);
inline std::string NameForObject(const zx::object_base& object) {
  return NameForObject(object.get());
}

// Returns the koids of the child objects of the given parent object. The
// child kind is passed to zx_object_get_info. It is typically
// ZX_INFO_PROCESS_THREADS, ZX_INFO_JOB_CHILDREN, or ZX_INFO_JOB_PROCESSES.
std::vector<zx_koid_t> GetChildKoids(zx_handle_t parent, uint32_t child_kind);

// Returns the specified kind of child objects.
std::vector<zx::job> GetChildJobs(zx_handle_t job);
std::vector<zx::process> GetChildProcesses(zx_handle_t job);
std::vector<zx::thread> GetChildThreads(zx_handle_t process);

}  // namespace debug_agent
