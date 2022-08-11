// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/system_interface.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_system_interface.h"

namespace debug_agent {

namespace {

TEST(SystemInterfaceTest, GetParentJobKoid) {
  auto system_interface = MockSystemInterface::CreateWithData();

  //  j: 1 root
  //    j: 8 job1  /moniker  fuchsia-pkg://devhost/package#meta/component.cm
  //      j: 13 job11
  //      j: 17 job12
  //        j: 18 job121
  //          p: 19 job121-p1
  EXPECT_EQ(17ull, system_interface->GetParentJobKoid(18));
  EXPECT_EQ(8ull, system_interface->GetParentJobKoid(17));
  EXPECT_EQ(8ull, system_interface->GetParentJobKoid(13));
  EXPECT_EQ(1ull, system_interface->GetParentJobKoid(8));
  EXPECT_EQ(ZX_KOID_INVALID, system_interface->GetParentJobKoid(1));
  EXPECT_EQ(ZX_KOID_INVALID, system_interface->GetParentJobKoid(19));
}

TEST(SystemInterfaceTest, GetComponentInfo) {
  auto system_interface = MockSystemInterface::CreateWithData();

  //  j: 1 root
  //    j: 8 job1  /moniker  fuchsia-pkg://devhost/package#meta/component.cm
  //      j: 17 job12
  //        j: 18 job121
  //          p: 19 job121-p1
  auto component_info = system_interface->GetComponentManager().FindComponentInfo(8);
  ASSERT_TRUE(component_info.has_value());
  EXPECT_EQ("/moniker", component_info->moniker);
  EXPECT_EQ("fuchsia-pkg://devhost/package#meta/component.cm", component_info->url);

  component_info =
      system_interface->GetComponentManager().FindComponentInfo(*system_interface->GetProcess(19));
  ASSERT_TRUE(component_info.has_value());
  EXPECT_EQ("/moniker", component_info->moniker);
  EXPECT_EQ("fuchsia-pkg://devhost/package#meta/component.cm", component_info->url);
}

}  // namespace

}  // namespace debug_agent
