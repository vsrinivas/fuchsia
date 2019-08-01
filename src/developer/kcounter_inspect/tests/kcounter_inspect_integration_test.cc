// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/cpp/fidl.h>
#include <garnet/public/lib/inspect_deprecated/reader.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async_promise/executor.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/vmo_file.h>

#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/substitute.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

// Confirm we can connect via FIDL to the real environment service
// fuchsia.kernel.Counter and retrieve something.
TEST(KcounterInspectIntegrationTest, FidlConnection) {
  fuchsia::kernel::CounterSyncPtr kcounter;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(kcounter.NewRequest());

  fuchsia::mem::Buffer buffer;
  zx_status_t status;
  ASSERT_TRUE(kcounter->GetInspectVMO(&status, &buffer) == ZX_OK);
  ASSERT_TRUE(status == ZX_OK);
}

std::vector<std::string> ToString(const inspect_deprecated::ObjectHierarchy& root,
                                  const std::string& indent = "") {
  std::vector<std::string> result;
  result.push_back(indent + root.name());
  for (const auto& metric : root.node().metrics()) {
    result.push_back(indent + "  " + metric.name());
  }
  for (const auto& child : root.children()) {
    auto child_result = ToString(child, indent + "  ");
    result.insert(result.end(), child_result.begin(), child_result.end());
  }
  return result;
}

bool Contains(const std::vector<std::string>& vec, const std::string& look_for) {
  for (const auto& v : vec) {
    if (v == look_for) {
      return true;
    }
  }
  return false;
}

TEST(KcounterInspectIntegrationTest, InspectReading) {
  fuchsia::kernel::CounterSyncPtr kcounter;
  auto environment_services = ::sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(kcounter.NewRequest());

  fuchsia::mem::Buffer buffer;
  zx_status_t status;
  ASSERT_TRUE(kcounter->GetInspectVMO(&status, &buffer) == ZX_OK);
  ASSERT_TRUE(status == ZX_OK);

  auto result = inspect_deprecated::ReadFromVmo(buffer.vmo);
  ASSERT_TRUE(result.is_ok());
  auto object_hierarchy = result.take_value();
  auto as_vector = ToString(object_hierarchy);
  EXPECT_TRUE(Contains(as_vector, "  handles"));
  EXPECT_TRUE(Contains(as_vector, "    handles.duped"));
  EXPECT_TRUE(Contains(as_vector, "    handles.live"));
  EXPECT_TRUE(Contains(as_vector, "    handles.made"));
  EXPECT_TRUE(Contains(as_vector, "        init.userboot.time.msec"));

  // There's no particular guarantee on update frequency, but at least ensure we
  // can call the update function, and that the VMO is readable after doing so.
  ASSERT_TRUE(kcounter->UpdateInspectVMO(&status) == ZX_OK);
  ASSERT_TRUE(status == ZX_OK);

  result = inspect_deprecated::ReadFromVmo(buffer.vmo);
  ASSERT_TRUE(result.is_ok());
  object_hierarchy = result.take_value();
  as_vector = ToString(object_hierarchy);
  EXPECT_TRUE(Contains(as_vector, "  handles"));
  EXPECT_TRUE(Contains(as_vector, "    handles.duped"));
  EXPECT_TRUE(Contains(as_vector, "    handles.live"));
  EXPECT_TRUE(Contains(as_vector, "    handles.made"));
  EXPECT_TRUE(Contains(as_vector, "        init.userboot.time.msec"));
}

}  // namespace
