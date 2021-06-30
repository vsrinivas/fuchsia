// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>

#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"
#include "src/graphics/tests/vkreadback/vkreadback.h"

TEST(VulkanExtension, ExternalMemoryFuchsia) {
  VkReadbackTest exported_test(VkReadbackTest::VK_FUCHSIA_EXTERNAL_MEMORY);
  ASSERT_TRUE(exported_test.Initialize(VK_API_VERSION_1_1));

  VkReadbackTest imported_test(exported_test.get_exported_memory_handle());
  ASSERT_TRUE(imported_test.Initialize(VK_API_VERSION_1_1));
  ASSERT_TRUE(exported_test.Exec());
  ASSERT_TRUE(imported_test.Readback());
}

TEST(VulkanExtension, GetMemoryZirconHandlePropertiesFUCHSIA) {
  std::vector<const char*> enabled_extension_names{VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME};

  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkreadback";
  app_info.apiVersion = VK_API_VERSION_1_1;

  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  // Copy the builder's default device info, which has its queue info
  // properly configured and modify the desired extension fields only.
  // Send the amended |device_info| back into the builder's
  // set_device_info() during unique context construction.
  VulkanContext::Builder builder;

  // TODO(fxbug.dev/73025): remove this disable when it's time.
  builder.set_validation_layers_enabled(false);

  vk::DeviceCreateInfo device_info = builder.DeviceInfo();
  device_info.enabledExtensionCount = enabled_extension_names.size();
  device_info.ppEnabledExtensionNames = enabled_extension_names.data();

  auto vulkan_context =
      builder.set_instance_info(instance_info).set_device_info(device_info).Unique();
  ASSERT_TRUE(vulkan_context);

  auto get_memory_zircon_handle_properties =
      reinterpret_cast<PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA>(
          vulkan_context->device()->getProcAddr("vkGetMemoryZirconHandlePropertiesFUCHSIA"));
  ASSERT_TRUE(get_memory_zircon_handle_properties);

  vk::MemoryZirconHandlePropertiesFUCHSIA handle_properties;
  reinterpret_cast<VkMemoryZirconHandlePropertiesFUCHSIA*>(&handle_properties)->sType =
      static_cast<VkStructureType>(VK_STRUCTURE_TYPE_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA);

  // TODO(fxbug.dev/69211): Emulator GPU devices (under device type
  // |eVirtualGpu|) cannot import arbitrary VMOs as VkDeviceMemory since it
  // doesn't have a unified memory architecture. Thus we skip this test case
  // and we'll need a new test case covering FEMU cases.
  auto phy_properties = vulkan_context->physical_device().getProperties();
  if (phy_properties.deviceType == vk::PhysicalDeviceType::eVirtualGpu) {
    fprintf(stderr,
            "Emulator GPU devices cannot support arbitrary VMOs, "
            "skipping test cases importing VMOs not exported from Vulkan\n");
  } else {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, vmo.create(4096, 0 /*options*/, &vmo));

    EXPECT_EQ(VK_SUCCESS,
              get_memory_zircon_handle_properties(
                  vulkan_context->device().get(),
                  VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA, vmo.get(),
                  reinterpret_cast<VkMemoryZirconHandlePropertiesFUCHSIA*>(&handle_properties)));
    EXPECT_NE(0u, handle_properties.memoryTypeBits);

    zx::vmo vmo_no_rights;
    ASSERT_EQ(ZX_OK, vmo.duplicate(0, &vmo_no_rights));
    vmo.reset();

    EXPECT_EQ(VK_SUCCESS,
              get_memory_zircon_handle_properties(
                  vulkan_context->device().get(),
                  VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA, vmo_no_rights.get(),
                  reinterpret_cast<VkMemoryZirconHandlePropertiesFUCHSIA*>(&handle_properties)));
    EXPECT_EQ(0u, handle_properties.memoryTypeBits);
  }

  constexpr uint32_t kGarbageHandle = 0xabcd1234;
  EXPECT_EQ(VK_ERROR_INVALID_EXTERNAL_HANDLE,
            get_memory_zircon_handle_properties(
                vulkan_context->device().get(),
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA, kGarbageHandle,
                reinterpret_cast<VkMemoryZirconHandlePropertiesFUCHSIA*>(&handle_properties)));
}
