// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_system_interface.h"

#include <string_view>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/debug_agent/zircon_job_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"
#include "src/developer/debug/shared/test_with_loop.h"

namespace debug_agent {

namespace {

// Recursively walks the process tree and returns true if there is a process
// matching the given koid. Fills the process_name if such process can be found.
// Fills the component_info if the process belongs to some component.
bool FindProcess(const debug_ipc::ProcessTreeRecord& record, zx_koid_t koid_to_find,
                 std::string* process_name, ComponentManager::ComponentInfo* component_info) {
  if (record.koid == koid_to_find) {
    *process_name = record.name;
    return true;
  }
  for (const auto& child : record.children) {
    if (FindProcess(child, koid_to_find, process_name, component_info)) {
      if (component_info->moniker.empty() && !record.component_url.empty()) {
        component_info->moniker = record.component_moniker;
        component_info->url = record.component_url;
      }
      return true;
    }
  }
  return false;
}

class SystemInfoTest : public debug::TestWithLoop {};

}  // namespace

TEST_F(SystemInfoTest, GetProcessTree) {
  ZirconSystemInterface system_interface;

  debug_ipc::ProcessTreeRecord root = system_interface.GetProcessTree();

  // The root node should be a job with some children.
  EXPECT_EQ(debug_ipc::ProcessTreeRecord::Type::kJob, root.type);
  EXPECT_FALSE(root.children.empty());

  // Query ourself.
  auto self = zx::process::self();
  zx_koid_t self_koid = zircon::KoidForObject(*self);
  ASSERT_NE(ZX_KOID_INVALID, self_koid);

  // Our koid should be somewhere in the tree.
  std::string process_name;
  ComponentManager::ComponentInfo component_info;
  EXPECT_TRUE(FindProcess(root, self_koid, &process_name, &component_info));

  // The process_name and component info should match
  EXPECT_EQ(zircon::NameForObject(*self), process_name);
  // The moniker is empty because it's actually "." in the test environment and the "." is stripped.
  EXPECT_EQ("", component_info.moniker);
  // The url will include a hash that cannot be compared.
  EXPECT_FALSE(component_info.url.empty());
  std::string_view prefix = "fuchsia-pkg://fuchsia.com/debug_agent_unit_tests";
  std::string_view suffix = "#meta/debug_agent_unit_tests.cm";
  EXPECT_EQ(prefix, component_info.url.substr(0, prefix.size()));
  EXPECT_EQ(suffix, component_info.url.substr(component_info.url.size() - suffix.size()));
}

}  // namespace debug_agent
