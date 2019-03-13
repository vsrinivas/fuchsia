// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../vkreadback/vkreadback.h"

TEST(VulkanExtension, ExternalMemoryFuchsiaKHR)
{
    VkReadbackTest export_app(VkReadbackTest::VK_KHR_EXTERNAL_MEMORY_FUCHSIA);
    VkReadbackTest import_app(VkReadbackTest::VK_KHR_EXTERNAL_MEMORY_FUCHSIA);

    ASSERT_TRUE(export_app.Initialize());
    import_app.set_device_memory_handle(export_app.get_device_memory_handle());
    ASSERT_TRUE(import_app.Initialize());
    ASSERT_TRUE(export_app.Exec());
    ASSERT_TRUE(import_app.Readback());
}
