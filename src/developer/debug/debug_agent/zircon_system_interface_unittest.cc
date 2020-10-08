// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/system_interface.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/zircon_job_handle.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"

namespace debug_agent {

namespace {

// Recursively walks the process tree and returns true if there is a process
// with the given name and koid.
bool FindProcess(const debug_ipc::ProcessTreeRecord& record, const std::string& name_to_find,
                 zx_koid_t koid_to_find) {
  if (record.name == name_to_find && record.koid == koid_to_find)
    return true;
  for (const auto& child : record.children) {
    if (FindProcess(child, name_to_find, koid_to_find))
      return true;
  }
  return false;
}

}  // namespace

TEST(SystemInfo, GetProcessTree) {
  ZirconSystemInterface system_interface;

  debug_ipc::ProcessTreeRecord root = system_interface.GetProcessTree();

  // The root node should be a job with some children.
  EXPECT_EQ(debug_ipc::ProcessTreeRecord::Type::kJob, root.type);
  EXPECT_FALSE(root.children.empty());

  // Compute our own process name and koid.
  auto self = zx::process::self();
  std::string self_name = zircon::NameForObject(*self);
  EXPECT_FALSE(self_name.empty());
  zx_koid_t self_koid = zircon::KoidForObject(*self);
  ASSERT_NE(0u, self_koid);

  // Our name and koid should be somewhere in the tree.
  EXPECT_TRUE(FindProcess(root, self_name, self_koid));
}

}  // namespace debug_agent
