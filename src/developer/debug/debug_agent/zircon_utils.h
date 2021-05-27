// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_UTILS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_UTILS_H_

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace debug_agent {
namespace zircon {

// This file contains low-level helpers for dealing with Zircon primitives.
//
// These should only be used by _zircon implementation files that aren't mocked. Normal callers
// (e.g. debugged_thread.cc) should go through the wrappers like ThreadHandle which allow mocking.
// These helpers are very low-level functions for use by the non-mocked implementations.
//
// These functions should work in terms of zx::* primitives and nto ProcessHandle, etc.

// Returns ZX_KOID_INVALID on failure.
zx_koid_t KoidForObject(const zx::object_base& object);

// Returns empty string on failure.
std::string NameForObject(const zx::object_base& object);

// Returns the given type of child koids.
std::vector<zx_koid_t> GetChildKoids(const zx::object_base& parent, uint32_t child_kind);

// Returns the given child objects. Will be empty on failure.
std::vector<zx::thread> GetChildThreads(const zx::process& process);
std::vector<zx::process> GetChildProcesses(const zx::job& job);
std::vector<zx::job> GetChildJobs(const zx::job& job);

}  // namespace zircon
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_UTILS_H_
