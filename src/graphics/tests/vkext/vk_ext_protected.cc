// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"
#include "src/lib/fsl/handles/object_info.h"
#include "vulkan_extension_test.h"

#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_handles.hpp"
#include <vulkan/vulkan.hpp>

namespace {
constexpr uint32_t kDefaultWidth = 64;
constexpr uint32_t kDefaultHeight = 64;
constexpr VkFormat kDefaultFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Parameter is true if the image should be linear.
class VulkanImageExtensionTest : public VulkanExtensionTest,
                                 public ::testing::WithParamInterface<bool> {};

TEST_P(VulkanImageExtensionTest, BufferCollectionProtectedRGBA) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, ProtectedAndNonprotectedConstraints) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, GetParam(), true));
}

TEST_P(VulkanImageExtensionTest, ProtectedCpuAccessible) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo =
      GetDefaultImageCreateInfo(true, kDefaultFormat, kDefaultWidth, kDefaultHeight, linear);

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;
  constraints_info.flags = vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
                           vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften;

  // This function should fail because protected images can't be CPU accessible.
  EXPECT_NE(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));
}

TEST_P(VulkanImageExtensionTest, ProtectedOptionalCompatible) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  for (uint32_t i = 0; i < 2; i++) {
    auto tokens = MakeSharedCollection(2u);

    bool linear = GetParam();
    bool protected_mem = (i == 0);
    auto image_create_info = GetDefaultImageCreateInfo(protected_mem, kDefaultFormat, kDefaultWidth,
                                                       kDefaultHeight, linear);
    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = image_create_info;

    auto image_create_info2 =
        GetDefaultImageCreateInfo(false, kDefaultFormat, kDefaultWidth, kDefaultHeight, linear);
    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints_2 =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints_2.imageCreateInfo = image_create_info2;

    UniqueBufferCollection collection1 =
        CreateVkBufferCollectionForImage(std::move(tokens[0]), format_constraints);

    UniqueBufferCollection collection2 = CreateVkBufferCollectionForImage(
        std::move(tokens[1]), format_constraints_2,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eProtectedOptional);

    vk::BufferCollectionPropertiesFUCHSIA properties;
    EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                        *collection1, &properties, loader_))
        << i;

    vk::BufferCollectionPropertiesFUCHSIA properties2;
    EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                        *collection2, &properties2, loader_))
        << i;
    EXPECT_EQ(properties.memoryTypeBits, properties2.memoryTypeBits) << i;

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
      if (properties.memoryTypeBits & (1 << i)) {
        EXPECT_EQ(protected_mem, !!(memory_properties.memoryTypes[i].propertyFlags &
                                    VK_MEMORY_PROPERTY_PROTECTED_BIT));
      }
    }

    // Use |image_create_info| for both because |image_create_info2| may not have the right flags
    // set.
    ASSERT_TRUE(InitializeDirectImage(*collection1, image_create_info));
    ASSERT_TRUE(InitializeDirectImage(*collection2, image_create_info));
  }
}

TEST_P(VulkanImageExtensionTest, ProtectedUnprotectedIncompatible) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  auto tokens = MakeSharedCollection(2u);

  bool linear = GetParam();

  vk::ImageFormatConstraintsInfoFUCHSIA constraints = GetDefaultRgbImageFormatConstraintsInfo();
  constraints.imageCreateInfo =
      GetDefaultImageCreateInfo(true, kDefaultFormat, kDefaultWidth, kDefaultHeight, linear);

  vk::ImageFormatConstraintsInfoFUCHSIA constraints2 = GetDefaultRgbImageFormatConstraintsInfo();
  constraints2.imageCreateInfo =
      GetDefaultImageCreateInfo(false, kDefaultFormat, kDefaultWidth, kDefaultHeight, linear);

  UniqueBufferCollection collection1 =
      CreateVkBufferCollectionForImage(std::move(tokens[0]), constraints);

  UniqueBufferCollection collection2 =
      CreateVkBufferCollectionForImage(std::move(tokens[1]), constraints2);

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_NE(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection1, &properties, loader_));
}

TEST_F(VulkanExtensionTest, BufferCollectionProtectedBuffer) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(ExecBuffer(16384));
}

INSTANTIATE_TEST_SUITE_P(, VulkanImageExtensionTest, ::testing::Bool(),
                         [](testing::TestParamInfo<bool> info) {
                           return info.param ? "Linear" : "Tiled";
                         });
}  // namespace
