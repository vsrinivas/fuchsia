// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../vkreadback/vkreadback.h"

TEST(VulkanExtension, ExternalMemoryFuchsia) {
  VkReadbackTest exported_test(VkReadbackTest::VK_FUCHSIA_EXTERNAL_MEMORY);
  ASSERT_TRUE(exported_test.Initialize());

  VkReadbackTest imported_test(exported_test.get_exported_memory_handle());
  ASSERT_TRUE(imported_test.Initialize());
  ASSERT_TRUE(exported_test.Exec());
  ASSERT_TRUE(imported_test.Readback());
}
