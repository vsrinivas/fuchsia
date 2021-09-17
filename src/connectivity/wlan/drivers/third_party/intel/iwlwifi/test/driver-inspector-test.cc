// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/driver-inspector.h"

#include <lib/inspect/cpp/hierarchy.h>

#include <cstring>
#include <vector>

#include <zxtest/zxtest.h>

namespace wlan::testing {
namespace {

// Test DriverInspector creation with different parameters.
TEST(DriverInspectorTest, CreationOptions) {
  auto d1 = wlan::iwlwifi::DriverInspector();
  EXPECT_TRUE(d1.GetRoot());

  auto d2 = wlan::iwlwifi::DriverInspector(
      wlan::iwlwifi::DriverInspectorOptions{.vmo_size = 4 * 1024, .core_dump_capacity = 4 * 1024});
  EXPECT_TRUE(d2.GetRoot());

  auto d3 = wlan::iwlwifi::DriverInspector(
      wlan::iwlwifi::DriverInspectorOptions{.vmo_size = 4 * 1024, .core_dump_capacity = 0});
  EXPECT_TRUE(d3.GetRoot());

  auto d4 = wlan::iwlwifi::DriverInspector(
      wlan::iwlwifi::DriverInspectorOptions{.vmo_size = 0, .core_dump_capacity = 0});
  EXPECT_FALSE(d4.GetRoot());
}

// Test DriverInspector core dump functionality.
TEST(DriverInspectorTest, PublishCoreDump) {
  auto inspector = wlan::iwlwifi::DriverInspector(
      wlan::iwlwifi::DriverInspectorOptions{.vmo_size = 8 * 1024, .core_dump_capacity = 2 * 1024});
  ASSERT_TRUE(inspector.GetRoot());

  auto large_buffer = std::vector<char>(2 * 1024 + 1);
  EXPECT_NOT_OK(inspector.PublishCoreDump("too large", large_buffer));

  // Create 5 buffers of 512 bytes each.
  std::vector<std::vector<char>> buffers;
  for (size_t i = 0; i < 5; ++i) {
    buffers.emplace_back(512, i);
  }

  // Insert the first 4 buffers as crash dumps, incrementally verifying that they all appear in the
  // Inspect hierarchy.
  for (size_t i = 0; i < 4; ++i) {
    char buffer_name[16];
    std::snprintf(buffer_name, sizeof(buffer_name), "buffer%zu", i);
    EXPECT_OK(inspector.PublishCoreDump(buffer_name, buffers[i]));

    auto hierarchy = inspector.GetHierarchy();
    auto& node = hierarchy.node();
    EXPECT_EQ(i + 1, node.properties().size());
    for (size_t j = 0; j < node.properties().size(); ++j) {
      char property_name[16];
      std::snprintf(property_name, sizeof(property_name), "buffer%zu", j);
      auto prop = node.get_property<inspect::ByteVectorPropertyValue>(property_name);
      EXPECT_NOT_NULL(prop);
      if (prop != nullptr) {
        EXPECT_EQ(512, prop->value().size());
        EXPECT_EQ(0, std::memcmp(buffers[j].data(), prop->value().data(),
                                 std::min(buffers[j].size(), prop->value().size())));
      }
    }
  }

  // Adding the fifth buffer should cause the oldest crash dump to be replaced.
  EXPECT_OK(inspector.PublishCoreDump("buffer4", buffers[4]));
  auto hierarchy = inspector.GetHierarchy();
  auto& node = hierarchy.node();
  EXPECT_EQ(4, node.properties().size());
  for (size_t j = 1; j < 5; ++j) {
    char property_name[16];
    std::snprintf(property_name, sizeof(property_name), "buffer%zu", j);
    auto prop = node.get_property<inspect::ByteVectorPropertyValue>(property_name);
    EXPECT_NOT_NULL(prop);
    if (prop != nullptr) {
      EXPECT_EQ(512, prop->value().size());
      EXPECT_EQ(0, std::memcmp(buffers[j].data(), prop->value().data(),
                               std::min(buffers[j].size(), prop->value().size())));
    }
  }
}

}  // namespace
}  // namespace wlan::testing
