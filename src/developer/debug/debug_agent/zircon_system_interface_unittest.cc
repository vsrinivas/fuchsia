// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_system_interface.h"

#include <string_view>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/filter.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/developer/debug/debug_agent/zircon_utils.h"
#include "src/developer/debug/shared/test_with_loop.h"

namespace debug_agent {

namespace {

// Recursively walks the process tree and returns true if there is a process
// matching the given koid. Fills the process_name if such process can be found.
// Fills the component_info if the process belongs to some component.
bool FindProcess(const debug_ipc::ProcessTreeRecord& record, zx_koid_t koid_to_find,
                 std::string* process_name,
                 std::optional<debug_ipc::ComponentInfo>* component_info) {
  if (record.koid == koid_to_find) {
    *process_name = record.name;
    return true;
  }
  for (const auto& child : record.children) {
    if (FindProcess(child, koid_to_find, process_name, component_info)) {
      if (!*component_info && record.component) {
        *component_info = record.component;
      }
      return true;
    }
  }
  return false;
}

class ZirconSystemInterfaceTest : public debug::TestWithLoop {};

TEST_F(ZirconSystemInterfaceTest, GetProcessTree) {
  ZirconSystemInterface system_interface;

  system_interface.zircon_component_manager().SetReadyCallback([&]() { loop().QuitNow(); });
  loop().Run();

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
  std::optional<debug_ipc::ComponentInfo> component_info;
  EXPECT_TRUE(FindProcess(root, self_koid, &process_name, &component_info));

  // The process_name and component info should match
  EXPECT_EQ(zircon::NameForObject(*self), process_name);
  // The moniker is empty because it's actually "." in the test environment and the "." is stripped.
  ASSERT_TRUE(component_info);
  EXPECT_EQ("", component_info->moniker);
  // The url will include a hash that cannot be compared.
  ASSERT_FALSE(component_info->url.empty());
  std::string_view prefix = "fuchsia-pkg://fuchsia.com/debug_agent_unit_tests";
  std::string_view suffix = "#meta/debug_agent_unit_tests.cm";
  ASSERT_GE(component_info->url.size(), prefix.size() + suffix.size());
  EXPECT_EQ(prefix, component_info->url.substr(0, prefix.size()));
  EXPECT_EQ(suffix, component_info->url.substr(component_info->url.size() - suffix.size()));
}

TEST_F(ZirconSystemInterfaceTest, FindComponentInfo) {
  ZirconSystemInterface system_interface;

  system_interface.zircon_component_manager().SetReadyCallback([&]() { loop().QuitNow(); });
  loop().Run();

  zx::process handle;
  zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  ZirconProcessHandle self(std::move(handle));

  auto component_info = system_interface.GetComponentManager().FindComponentInfo(self);

  ASSERT_TRUE(component_info);
  // The moniker is empty because it's actually "." in the test environment and the "." is stripped.
  EXPECT_EQ("", component_info->moniker);
  // The url will include a hash that cannot be compared.
  ASSERT_FALSE(component_info->url.empty());
  std::string_view prefix = "fuchsia-pkg://fuchsia.com/debug_agent_unit_tests";
  std::string_view suffix = "#meta/debug_agent_unit_tests.cm";
  ASSERT_GE(component_info->url.size(), prefix.size() + suffix.size());
  EXPECT_EQ(prefix, component_info->url.substr(0, prefix.size()));
  EXPECT_EQ(suffix, component_info->url.substr(component_info->url.size() - suffix.size()));
}

TEST_F(ZirconSystemInterfaceTest, FilterMatchProcess) {
  ZirconSystemInterface system_interface;

  system_interface.zircon_component_manager().SetReadyCallback([&]() { loop().QuitNow(); });
  loop().Run();

  zx::process handle;
  zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  ZirconProcessHandle self(std::move(handle));

  debug_ipc::Filter filter{.type = debug_ipc::Filter::Type::kComponentName,
                           .pattern = "debug_agent_unit_tests.cm"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(self, system_interface));

  filter = {
      .type = debug_ipc::Filter::Type::kComponentUrl,
      .pattern = "fuchsia-pkg://fuchsia.com/debug_agent_unit_tests#meta/debug_agent_unit_tests.cm"};
  EXPECT_TRUE(Filter(filter).MatchesProcess(self, system_interface));
}

}  // namespace

}  // namespace debug_agent
