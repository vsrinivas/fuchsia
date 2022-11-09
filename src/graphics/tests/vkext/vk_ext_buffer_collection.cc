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

#include <fbl/algorithm.h>
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
constexpr VkFormat kDefaultYuvFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

// Parameter is true if the image should be linear.
class VulkanImageExtensionTest : public VulkanExtensionTest,
                                 public ::testing::WithParamInterface<bool> {};

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12_1026) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();

  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 1026, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA_1026) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 1026, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();

  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionI420) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();

  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 64, 64, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12_1280_546) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();

  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 8192, 546, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionMultipleFormats) {
  ASSERT_TRUE(Initialize());

  fuchsia::sysmem::ImageFormatConstraints nv12_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  nv12_image_constraints.pixel_format = {fuchsia::sysmem::PixelFormatType::NV12, false};
  nv12_image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  fuchsia::sysmem::ImageFormatConstraints bgra_tiled_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  bgra_tiled_image_constraints.pixel_format = {
      fuchsia::sysmem::PixelFormatType::BGRA32,
      true,
      {fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED}};
  std::vector<fuchsia::sysmem::ImageFormatConstraints> all_constraints{
      nv12_image_constraints, bgra_image_constraints, bgra_tiled_image_constraints};

  if (!UseVirtualGpu()) {
    ASSERT_TRUE(
        Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, GetParam(), false, all_constraints));
  }
  vk_device_memory_ = {};
  ASSERT_TRUE(Exec(VK_FORMAT_B8G8R8A8_UNORM, 64, 64, GetParam(), false, all_constraints));
}

TEST_P(VulkanImageExtensionTest, MultiImageFormatEntrypoint) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  auto image_create_info = GetDefaultImageCreateInfo(use_protected_memory_, kDefaultFormat,
                                                     kDefaultWidth, kDefaultHeight, linear);

  vk::ImageFormatConstraintsInfoFUCHSIA constraints = GetDefaultRgbImageFormatConstraintsInfo();
  constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), constraints);

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(kDefaultFormat, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));
}

TEST_P(VulkanImageExtensionTest, R8) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token, sysmem_token] = MakeSharedCollection<2>();

  bool linear = GetParam();
  // TODO(fxbug.dev/59804): Enable the test on emulators when goldfish host-visible heap
  // supports R8 linear images.
  if (linear && UseVirtualGpu())
    GTEST_SKIP();

  auto image_create_info = GetDefaultImageCreateInfo(use_protected_memory_, VK_FORMAT_R8_UNORM,
                                                     kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA constraints = GetDefaultRgbImageFormatConstraintsInfo();
  constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), constraints);

  auto sysmem_collection_info = AllocateSysmemCollection({}, std::move(sysmem_token));
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::R8,
            sysmem_collection_info.settings.image_format_constraints.pixel_format.type);

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(VK_FORMAT_R8_UNORM, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
  EXPECT_EQ(static_cast<uint32_t>(fuchsia::sysmem::PixelFormatType::R8),
            properties.sysmemPixelFormat);
}

TEST_P(VulkanImageExtensionTest, R8G8) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  // TODO(fxbug.dev/59804): Enable the test on emulators when goldfish host-visible heap
  // supports R8G8 linear images.
  if (linear && UseVirtualGpu())
    GTEST_SKIP();

  auto image_create_info = GetDefaultImageCreateInfo(use_protected_memory_, VK_FORMAT_R8G8_UNORM,
                                                     kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA constraints = GetDefaultRgbImageFormatConstraintsInfo();
  constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), constraints);

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(VK_FORMAT_R8G8_UNORM, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));
}

TEST_P(VulkanImageExtensionTest, R8ToL8) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token, sysmem_token] = MakeSharedCollection<2>();

  bool linear = GetParam();
  // TODO(fxbug.dev/59804): Enable the test on emulators when goldfish host-visible heap
  // supports R8/L8 linear images.
  if (linear && UseVirtualGpu())
    GTEST_SKIP();

  auto image_create_info = GetDefaultImageCreateInfo(use_protected_memory_, VK_FORMAT_R8_UNORM,
                                                     kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.sysmemPixelFormat =
      static_cast<uint64_t>(fuchsia::sysmem::PixelFormatType::L8);
  format_constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), format_constraints);

  auto sysmem_collection_info = AllocateSysmemCollection({}, std::move(sysmem_token));
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::L8,
            sysmem_collection_info.settings.image_format_constraints.pixel_format.type);

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(VK_FORMAT_R8_UNORM, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
  EXPECT_EQ(static_cast<uint32_t>(fuchsia::sysmem::PixelFormatType::L8),
            properties.sysmemPixelFormat);
}

TEST_P(VulkanImageExtensionTest, NonPackedImage) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token, sysmem_token] = MakeSharedCollection<2>();

  bool linear = GetParam();

  auto image_create_info = GetDefaultImageCreateInfo(
      use_protected_memory_, VK_FORMAT_B8G8R8A8_UNORM, kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), format_constraints);

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = GetDefaultSysmemImageFormatConstraints();
  constraints.image_format_constraints[0].min_coded_width = 64;
  constraints.image_format_constraints[0].min_bytes_per_row = 1024;
  auto sysmem_collection_info = AllocateSysmemCollection(constraints, std::move(sysmem_token));

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(VK_FORMAT_R8_UNORM, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
}

TEST_P(VulkanImageExtensionTest, ImageCpuAccessible) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  auto image_create_info = GetDefaultImageCreateInfo(use_protected_memory_, kDefaultFormat,
                                                     kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), format_constraints,
                                       vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
                                           vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

  ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

  if (linear) {
    CheckLinearSubresourceLayout(kDefaultFormat, kDefaultWidth);
  }

  ASSERT_TRUE(InitializeDirectImageMemory(*collection));
  {
    // Check that all memory types are host visible.
    vk::BufferCollectionPropertiesFUCHSIA properties;
    vk::Result result1 =
        ctx_->device()->getBufferCollectionPropertiesFUCHSIA(*collection, &properties, loader_);
    EXPECT_EQ(result1, vk::Result::eSuccess);

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
      if (properties.memoryTypeBits & (1 << i)) {
        EXPECT_TRUE(memory_properties.memoryTypes[i].propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (!(memory_properties.memoryTypes[i].propertyFlags &
              VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
          printf(
              "WARNING: read-often buffer may be using non-cached memory. This will work but may "
              "be slow.\n");
          fflush(stdout);
        }
      }
    }
  }
  void *data;
  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()->mapMemory(*vk_device_memory_, 0, VK_WHOLE_SIZE, {}, &data));
  auto volatile_data = static_cast<volatile uint8_t *>(data);
  *volatile_data = 1;

  EXPECT_EQ(1u, *volatile_data);
}

TEST_P(VulkanImageExtensionTest, BadSysmemFormat) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token] = MakeSharedCollection<1>();

  constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
  bool linear = GetParam();
  auto image_create_info =
      GetDefaultImageCreateInfo(false, kFormat, kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo = image_create_info;
  format_constraints.sysmemPixelFormat = static_cast<int>(fuchsia::sysmem::PixelFormatType::NV12);

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  // NV12 and R8G8B8A8 aren't compatible, so combining them should fail.
  EXPECT_NE(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));
}

TEST_P(VulkanImageExtensionTest, BadColorSpace) {
  ASSERT_TRUE(Initialize());
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();

  std::array<vk::SysmemColorSpaceFUCHSIA, 2> color_spaces;
  color_spaces[0].colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_NTSC);
  color_spaces[1].colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo =
      GetDefaultImageCreateInfo(false, kDefaultFormat, kDefaultWidth, kDefaultHeight, linear);
  format_constraints.pColorSpaces = color_spaces.data();
  format_constraints.colorSpaceCount = color_spaces.size();

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));
  // REC601 and REC709 aren't compatible with R8G8B8A8, so allocation should fail.
  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_NE(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
}

TEST_P(VulkanImageExtensionTest, YUVProperties) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto [vulkan_token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  std::array<vk::SysmemColorSpaceFUCHSIA, 1> color_spaces;
  color_spaces[0].colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultYuvImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo =
      GetDefaultImageCreateInfo(false, kDefaultYuvFormat, kDefaultWidth, kDefaultHeight, linear);
  format_constraints.pColorSpaces = color_spaces.data();
  format_constraints.colorSpaceCount = color_spaces.size();
  format_constraints.sysmemPixelFormat =
      static_cast<uint64_t>(fuchsia::sysmem::PixelFormatType::NV12);

  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), format_constraints);

  vk::BufferCollectionPropertiesFUCHSIA properties;
  ASSERT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
  EXPECT_EQ(static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709),
            properties.sysmemColorSpaceIndex.colorSpace);
  EXPECT_EQ(static_cast<uint32_t>(fuchsia::sysmem::PixelFormatType::NV12),
            properties.sysmemPixelFormat);
  EXPECT_EQ(0u, properties.createInfoIndex);
  EXPECT_EQ(1u, properties.bufferCount);
  EXPECT_TRUE(properties.formatFeatures & vk::FormatFeatureFlagBits::eSampledImage);

  // The driver could represent these differently, but all current drivers want the identity.
  EXPECT_EQ(vk::ComponentSwizzle::eIdentity, properties.samplerYcbcrConversionComponents.r);
  EXPECT_EQ(vk::ComponentSwizzle::eIdentity, properties.samplerYcbcrConversionComponents.g);
  EXPECT_EQ(vk::ComponentSwizzle::eIdentity, properties.samplerYcbcrConversionComponents.b);
  EXPECT_EQ(vk::ComponentSwizzle::eIdentity, properties.samplerYcbcrConversionComponents.a);

  EXPECT_EQ(vk::SamplerYcbcrModelConversion::eYcbcr709, properties.suggestedYcbcrModel);
  EXPECT_EQ(vk::SamplerYcbcrRange::eItuNarrow, properties.suggestedYcbcrRange);

  // Match h.264 default sitings by default.
  EXPECT_EQ(vk::ChromaLocation::eCositedEven, properties.suggestedXChromaOffset);
  EXPECT_EQ(vk::ChromaLocation::eMidpoint, properties.suggestedYChromaOffset);
}

// Check that if a collection could be used with two different formats, that sysmem can negotiate a
// common format.
TEST_P(VulkanImageExtensionTest, MultiFormat) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto tokens = MakeSharedCollection(2u);

  bool linear = GetParam();
  auto nv12_create_info =
      GetDefaultImageCreateInfo(false, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 1, 1, linear);
  auto rgb_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, linear);
  auto rgb_create_info_full_size = GetDefaultImageCreateInfo(false, VK_FORMAT_R8G8B8A8_UNORM,
                                                             kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints_info =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints_info.imageCreateInfo = rgb_create_info;

  std::vector<UniqueBufferCollection> collections;
  for (uint32_t i = 0; i < 2; i++) {
    vk::BufferCollectionCreateInfoFUCHSIA import_info(tokens[i].Unbind().TakeChannel().release());
    auto [result, collection] =
        ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
    EXPECT_EQ(result, vk::Result::eSuccess);
    collections.push_back(std::move(collection));
  }

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints_info;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCountForCamping = 1;
  constraints_info.bufferCollectionConstraints.minBufferCountForSharedSlack = 2;
  constraints_info.bufferCollectionConstraints.minBufferCountForDedicatedSlack = 3;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[0], constraints_info, loader_));

  std::array<vk::ImageFormatConstraintsInfoFUCHSIA, 2> format_constraints_infos = {
      GetDefaultYuvImageFormatConstraintsInfo(),
      GetDefaultRgbImageFormatConstraintsInfo(),
  };
  format_constraints_infos[0].imageCreateInfo = nv12_create_info;
  format_constraints_infos[1].imageCreateInfo = rgb_create_info_full_size;

  constraints_info.pFormatConstraints = format_constraints_infos.data();
  constraints_info.formatConstraintsCount = format_constraints_infos.size();

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[1], constraints_info, loader_));

  const uint32_t kExpectedImageCount =
      constraints_info.bufferCollectionConstraints.minBufferCountForCamping * 2 +
      constraints_info.bufferCollectionConstraints.minBufferCountForDedicatedSlack * 2 +
      constraints_info.bufferCollectionConstraints.minBufferCountForSharedSlack;
  for (uint32_t i = 0; i < 2; i++) {
    vk::BufferCollectionPropertiesFUCHSIA properties;
    ASSERT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                        *collections[i], &properties, loader_));
    EXPECT_EQ(i == 0 ? 0u : 1u, properties.createInfoIndex);
    EXPECT_EQ(kExpectedImageCount, properties.bufferCount);
    EXPECT_TRUE(properties.formatFeatures & vk::FormatFeatureFlagBits::eSampledImage);
  }
  vk::BufferCollectionImageCreateInfoFUCHSIA image_format_fuchsia;
  image_format_fuchsia.collection = *collections[0];
  image_format_fuchsia.index = 3;
  rgb_create_info_full_size.pNext = &image_format_fuchsia;

  auto [result, vk_image] = ctx_->device()->createImageUnique(rgb_create_info_full_size, nullptr);
  EXPECT_EQ(result, vk::Result::eSuccess);
  vk_image_ = std::move(vk_image);

  ASSERT_TRUE(InitializeDirectImageMemory(*collections[0], kExpectedImageCount));
}

TEST_P(VulkanImageExtensionTest, MaxBufferCountCheck) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto tokens = MakeSharedCollection(2u);

  bool linear = GetParam();
  auto nv12_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
                                                    kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints_info =
      GetDefaultYuvImageFormatConstraintsInfo();
  format_constraints_info.imageCreateInfo = nv12_create_info;

  std::vector<UniqueBufferCollection> collections;
  for (uint32_t i = 0; i < 2; i++) {
    vk::BufferCollectionCreateInfoFUCHSIA import_info(tokens[i].Unbind().TakeChannel().release());
    auto [result, collection] =
        ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
    EXPECT_EQ(result, vk::Result::eSuccess);
    collections.push_back(std::move(collection));
  }

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints_info;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;
  constraints_info.bufferCollectionConstraints.maxBufferCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCountForCamping = 1;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[0], constraints_info, loader_));

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[1], constraints_info, loader_));

  // Total buffer count for camping (2) exceeds maxBufferCount, so allocation should fail.
  for (auto &collection : collections) {
    vk::BufferCollectionPropertiesFUCHSIA properties;
    EXPECT_NE(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                        *collection, &properties, loader_));
  }
}

TEST_P(VulkanImageExtensionTest, ManyIdenticalFormats) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto [token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  auto nv12_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
                                                    kDefaultWidth, kDefaultHeight, linear);

  vk::BufferCollectionCreateInfoFUCHSIA import_info(token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  // All create info are identical, so the driver should be able to deduplicate them even though
  // there are more formats than sysmem supports.
  std::vector<vk::ImageFormatConstraintsInfoFUCHSIA> format_constraints_infos(
      64, GetDefaultYuvImageFormatConstraintsInfo());
  for (uint32_t i = 0; i < format_constraints_infos.size(); i++) {
    format_constraints_infos[i].imageCreateInfo = nv12_create_info;
  }
  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = format_constraints_infos.data();
  constraints_info.formatConstraintsCount = static_cast<uint32_t>(format_constraints_infos.size());
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  ASSERT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
  EXPECT_GT(format_constraints_infos.size(), properties.createInfoIndex);
}

// Check that createInfoIndex keeps track of multiple colorspaces properly.
TEST_P(VulkanImageExtensionTest, ColorSpaceSubset) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto tokens = MakeSharedCollection(2u);

  bool linear = GetParam();
  auto nv12_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
                                                    kDefaultWidth, kDefaultHeight, linear);

  std::vector<UniqueBufferCollection> collections;
  for (uint32_t i = 0; i < 2; i++) {
    vk::BufferCollectionCreateInfoFUCHSIA import_info(tokens[i].Unbind().TakeChannel().release());
    auto [result, collection] =
        ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
    EXPECT_EQ(result, vk::Result::eSuccess);
    collections.push_back(std::move(collection));
  }

  // Two different create info, where the only difference is the supported set of sysmem
  // colorspaces.
  std::array<vk::ImageFormatConstraintsInfoFUCHSIA, 2> format_constraints = {
      GetDefaultYuvImageFormatConstraintsInfo(),
      GetDefaultYuvImageFormatConstraintsInfo(),
  };
  format_constraints[0].imageCreateInfo = nv12_create_info;
  format_constraints[1].imageCreateInfo = nv12_create_info;

  std::array<vk::SysmemColorSpaceFUCHSIA, 2> color_spaces_601;
  color_spaces_601[0].colorSpace =
      static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_NTSC);
  color_spaces_601[1].colorSpace =
      static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_PAL);
  format_constraints[0].setColorSpaceCount(color_spaces_601.size());
  format_constraints[0].setPColorSpaces(color_spaces_601.data());
  vk::SysmemColorSpaceFUCHSIA color_space_709;
  color_space_709.colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709);
  format_constraints[1].setColorSpaceCount(1);
  format_constraints[1].setPColorSpaces(&color_space_709);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = format_constraints.data();
  constraints_info.formatConstraintsCount = format_constraints.size();
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[0], constraints_info, loader_));

  constraints_info.pFormatConstraints = &format_constraints[1];
  constraints_info.formatConstraintsCount = 1;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collections[1], constraints_info, loader_));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collections[0], &properties, loader_));
  EXPECT_EQ(1u, properties.createInfoIndex);
}

TEST_P(VulkanImageExtensionTest, WeirdFormat) {
  ASSERT_TRUE(Initialize());
  // TODO(fxbug.dev/59804): Enable the test when YUV sysmem images are
  // supported on emulators.
  if (UseVirtualGpu())
    GTEST_SKIP();
  auto [token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  auto nv12_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
                                                    kDefaultWidth, kDefaultHeight, linear);
  // Currently there's no sysmem format corresponding to R16G16B16, so this format should just be
  // ignored.
  auto rgb16_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_R16G16B16_SSCALED,
                                                     kDefaultWidth, kDefaultHeight, linear);

  vk::BufferCollectionCreateInfoFUCHSIA import_info(token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  std::array<vk::ImageFormatConstraintsInfoFUCHSIA, 2> format_constraints = {
      GetDefaultRgbImageFormatConstraintsInfo(),
      GetDefaultYuvImageFormatConstraintsInfo(),
  };
  format_constraints[0].imageCreateInfo = rgb16_create_info;
  format_constraints[1].imageCreateInfo = nv12_create_info;
  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = format_constraints.data();
  constraints_info.formatConstraintsCount = format_constraints.size();
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));

  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &properties, loader_));
  EXPECT_EQ(1u, properties.createInfoIndex);
}

TEST_P(VulkanImageExtensionTest, NoValidFormat) {
  ASSERT_TRUE(Initialize());
  auto [token] = MakeSharedCollection<1>();

  bool linear = GetParam();
  auto rgb16_create_info = GetDefaultImageCreateInfo(false, VK_FORMAT_R16G16B16_SSCALED,
                                                     kDefaultWidth, kDefaultHeight, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultRgbImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo = rgb16_create_info;

  vk::BufferCollectionCreateInfoFUCHSIA import_info(token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  // Currently there's no sysmem format corresponding to R16G16B16, so this should return an error
  // since no input format is valid.
  EXPECT_EQ(vk::Result::eErrorFormatNotSupported,
            ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(*collection,
                                                                       constraints_info, loader_));
}

INSTANTIATE_TEST_SUITE_P(, VulkanImageExtensionTest, ::testing::Bool(),
                         [](testing::TestParamInfo<bool> info) {
                           return info.param ? "Linear" : "Tiled";
                         });

// Check that linear and optimal images are compatible with each other.
TEST_F(VulkanExtensionTest, LinearOptimalCompatible) {
  ASSERT_TRUE(Initialize());
  auto tokens = MakeSharedCollection(2u);

  auto linear_create_info =
      GetDefaultImageCreateInfo(false, kDefaultFormat, kDefaultWidth, kDefaultHeight, true);
  auto optimal_create_info =
      GetDefaultImageCreateInfo(false, kDefaultFormat, kDefaultWidth, kDefaultHeight, false);

  std::vector<UniqueBufferCollection> collections;
  for (uint32_t i = 0; i < 2; i++) {
    vk::BufferCollectionCreateInfoFUCHSIA import_info(tokens[i].Unbind().TakeChannel().release());
    auto [result, collection] =
        ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
    EXPECT_EQ(result, vk::Result::eSuccess);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = i == 0 ? linear_create_info : optimal_create_info;

    vk::ImageConstraintsInfoFUCHSIA constraints_info;
    constraints_info.pFormatConstraints = &format_constraints;
    constraints_info.formatConstraintsCount = 1;
    constraints_info.bufferCollectionConstraints.minBufferCount = 1;

    EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                        *collection, constraints_info, loader_));
    collections.push_back(std::move(collection));
  }
  for (uint32_t i = 0; i < 2; i++) {
    // Use the same info as was originally used when setting constraints.
    vk::ImageCreateInfo info = i == 0 ? linear_create_info : optimal_create_info;
    vk::BufferCollectionImageCreateInfoFUCHSIA image_format_fuchsia;
    image_format_fuchsia.collection = *collections[i];
    info.pNext = &image_format_fuchsia;

    auto [result, vk_image] = ctx_->device()->createImageUnique(info, nullptr);
    EXPECT_EQ(result, vk::Result::eSuccess);
    vk_image_ = std::move(vk_image);
    if (i == 0)
      CheckLinearSubresourceLayout(kDefaultFormat, kDefaultWidth);

    ASSERT_TRUE(InitializeDirectImageMemory(*collections[i], 1));

    vk_device_memory_ = {};
  }
}

TEST_F(VulkanExtensionTest, BadRequiredFormatFeatures) {
  ASSERT_TRUE(Initialize());

  auto [vulkan_token] = MakeSharedCollection<1>();

  constexpr VkFormat kFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  constexpr bool kLinear = false;

  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultYuvImageFormatConstraintsInfo();
  format_constraints.imageCreateInfo =
      GetDefaultImageCreateInfo(false, kFormat, kDefaultWidth, kDefaultHeight, kLinear);
  format_constraints.requiredFormatFeatures = vk::FormatFeatureFlagBits::eVertexBuffer;

  auto properties = ctx_->physical_device().getFormatProperties(vk::Format(kFormat));

  if ((properties.linearTilingFeatures & format_constraints.requiredFormatFeatures) ==
      format_constraints.requiredFormatFeatures) {
    printf("Linear supports format features");
    fflush(stdout);
    GTEST_SKIP();
    return;
  }
  if ((properties.optimalTilingFeatures & format_constraints.requiredFormatFeatures) ==
      format_constraints.requiredFormatFeatures) {
    printf("Optimal supports format features");
    fflush(stdout);
    GTEST_SKIP();
    return;
  }

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = &format_constraints;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  // Creating the constraints should fail because the driver doesn't support the features with
  // either linear or optimal.
  EXPECT_NE(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));
}

TEST_F(VulkanExtensionTest, BadRequiredFormatFeatures2) {
  ASSERT_TRUE(Initialize());

  auto [vulkan_token] = MakeSharedCollection<1>();

  const VkFormat kFormat =
      UseVirtualGpu() ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  bool is_yuv = kFormat == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  constexpr bool kLinear = false;
  auto image_create_info =
      GetDefaultImageCreateInfo(false, kFormat, kDefaultWidth, kDefaultHeight, kLinear);

  auto properties = ctx_->physical_device().getFormatProperties(vk::Format(kFormat));

  vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
      GetDefaultImageFormatConstraintsInfo(is_yuv);
  format_constraints.requiredFormatFeatures = vk::FormatFeatureFlagBits::eVertexBuffer;

  if ((properties.linearTilingFeatures & format_constraints.requiredFormatFeatures) ==
      format_constraints.requiredFormatFeatures) {
    printf("Linear supports format features");
    fflush(stdout);
    GTEST_SKIP();
    return;
  }
  if ((properties.optimalTilingFeatures & format_constraints.requiredFormatFeatures) ==
      format_constraints.requiredFormatFeatures) {
    printf("Optimal supports format features");
    fflush(stdout);
    GTEST_SKIP();
    return;
  }

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  std::array<vk::ImageFormatConstraintsInfoFUCHSIA, 2> format_infos{
      format_constraints, GetDefaultImageFormatConstraintsInfo(is_yuv)};
  format_infos[0].imageCreateInfo = image_create_info;
  format_infos[1].imageCreateInfo = image_create_info;

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.pFormatConstraints = format_infos.data();
  constraints_info.formatConstraintsCount = format_infos.size();
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;

  // The version with a invalid format feature should fail, but the one with an allowed format
  // feature should allow everything to continue.
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(
                                      *collection, constraints_info, loader_));
  vk::BufferCollectionPropertiesFUCHSIA collection_properties;
  EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->getBufferCollectionPropertiesFUCHSIA(
                                      *collection, &collection_properties, loader_));
  EXPECT_EQ(1u, collection_properties.createInfoIndex);
}

TEST_F(VulkanExtensionTest, BufferCollectionBuffer1024) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ExecBuffer(1024));
}

TEST_F(VulkanExtensionTest, BufferCollectionBuffer16384) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ExecBuffer(16384));
}

TEST_F(VulkanExtensionTest, ImportAliasing) {
  ASSERT_TRUE(Initialize());

  constexpr bool kUseProtectedMemory = false;
  constexpr bool kUseLinear = true;
  constexpr uint32_t kSrcHeight = kDefaultHeight;
  constexpr uint32_t kDstHeight = kSrcHeight * 2;
  constexpr uint32_t kPattern = 0xaabbccdd;

  vk::UniqueImage src_image1, src_image2;
  vk::UniqueDeviceMemory src_memory1, src_memory2;

  {
    auto [vulkan_token] = MakeSharedCollection<1>();

    vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, kDefaultFormat, kDefaultWidth, kSrcHeight, kUseLinear);
    image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
    image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = image_create_info;

    UniqueBufferCollection collection = CreateVkBufferCollectionForImage(
        std::move(vulkan_token), format_constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
            vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

    std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection);
    ASSERT_TRUE(init_img_memory_result);
    uint32_t memoryTypeIndex = init_img_memory_result.value();
    bool src_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

    src_image1 = std::move(vk_image_);
    src_memory1 = std::move(vk_device_memory_);

    WriteLinearImage(src_memory1.get(), src_is_coherent, kDefaultWidth, kSrcHeight, kPattern);

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));
    ASSERT_TRUE(InitializeDirectImageMemory(*collection));

    // src2 is alias of src1
    src_image2 = std::move(vk_image_);
    src_memory2 = std::move(vk_device_memory_);
  }

  vk::UniqueImage dst_image;
  vk::UniqueDeviceMemory dst_memory;
  bool dst_is_coherent;

  {
    auto [vulkan_token] = MakeSharedCollection<1>();

    vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, kDefaultFormat, kDefaultWidth, kDstHeight, kUseLinear);
    image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferDst);
    image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = image_create_info;

    UniqueBufferCollection collection = CreateVkBufferCollectionForImage(
        std::move(vulkan_token), format_constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
            vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

    std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection);
    ASSERT_TRUE(init_img_memory_result);
    uint32_t memoryTypeIndex = init_img_memory_result.value();
    dst_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

    dst_image = std::move(vk_image_);
    dst_memory = std::move(vk_device_memory_);

    WriteLinearImage(dst_memory.get(), dst_is_coherent, kDefaultWidth, kDstHeight, 0xffffffff);
  }

  vk::UniqueCommandPool command_pool;
  {
    auto info =
        vk::CommandPoolCreateInfo().setQueueFamilyIndex(vulkan_context().queue_family_index());
    auto result = vulkan_context().device()->createCommandPoolUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_pool = std::move(result.value);
  }

  std::vector<vk::UniqueCommandBuffer> command_buffers;
  {
    auto info = vk::CommandBufferAllocateInfo()
                    .setCommandPool(command_pool.get())
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(1);
    auto result = vulkan_context().device()->allocateCommandBuffersUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_buffers = std::move(result.value);
  }

  {
    auto info = vk::CommandBufferBeginInfo();
    EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->begin(&info));
  }

  for (vk::Image image : std::vector<vk::Image>{src_image1.get(), src_image2.get()}) {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(image)
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  {
    auto layer = vk::ImageSubresourceLayers()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLayerCount(1);
    auto copy1 = vk::ImageCopy()
                     .setSrcSubresource(layer)
                     .setDstSubresource(layer)
                     .setSrcOffset({0, 0, 0})
                     .setDstOffset({0, 0, 0})
                     .setExtent({kDefaultWidth, kSrcHeight, 1});
    command_buffers[0]->copyImage(src_image1.get(), vk::ImageLayout::eTransferSrcOptimal,
                                  dst_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copy1);
    auto copy2 = vk::ImageCopy()
                     .setSrcSubresource(layer)
                     .setDstSubresource(layer)
                     .setSrcOffset({0, 0, 0})
                     .setDstOffset({0, kSrcHeight, 0})
                     .setExtent({kDefaultWidth, kSrcHeight, 1});
    command_buffers[0]->copyImage(src_image2.get(), vk::ImageLayout::eTransferSrcOptimal,
                                  dst_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copy2);
  }
  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eHostRead)
                       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setNewLayout(vk::ImageLayout::eGeneral)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, /* srcStageMask */
        vk::PipelineStageFlagBits::eHost,     /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->end());

  {
    auto command_buffer_temp = command_buffers[0].get();
    auto info = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&command_buffer_temp);
    EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().submit(1, &info, vk::Fence()));
  }

  EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().waitIdle());

  CheckLinearImage(dst_memory.get(), dst_is_coherent, kDefaultWidth, kDstHeight, kPattern);
}

class VulkanFormatTest : public VulkanExtensionTest,
                         public ::testing::WithParamInterface<VkFormat> {};

TEST(ByteOffsetCalculation, YTiling) {
  // In pixels. 2 tiles by 2 tiles.
  constexpr size_t kWidth = 256 / 4;
  constexpr size_t kHeight = 64;
  std::vector<uint32_t> tile_data(4096 * 2 * 2);
  fuchsia::sysmem::BufferCollectionInfo_2 info;
  info.settings.has_image_format_constraints = true;
  auto &image_format_constraints = info.settings.image_format_constraints;
  image_format_constraints.pixel_format.format_modifier.value =
      fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED;
  image_format_constraints.bytes_per_row_divisor = 256;
  for (size_t y = 0; y < kHeight; y++) {
    for (size_t x = 0; x < kWidth; x++) {
      size_t offset = GetImageByteOffset(x, y, info, kWidth, kHeight);
      EXPECT_EQ(offset % 4, 0u);
      tile_data[offset]++;
    }
  }
  // Every pixel should be returned once.
  for (size_t i = 0; i < tile_data.size(); i += 4) {
    EXPECT_EQ(tile_data[i], 1u);
  }
  EXPECT_EQ(0u, GetImageByteOffset(0, 0, info, kWidth, kHeight));
  constexpr uint32_t kOWordSize = 16;
  // Spot check that (0, 1) starts the next OWord after (0, 0).
  EXPECT_EQ(kOWordSize, GetImageByteOffset(0, 1, info, kWidth, kHeight));
  // Spot check that (4, 0) (the beginning of the next OWord horizontally) occurs after all 32 rows.
  EXPECT_EQ(32u * kOWordSize, GetImageByteOffset(kOWordSize / 4, 0, info, kWidth, kHeight));
}

// Test that any fast clears are resolved by a foreign queue transition.
TEST_P(VulkanFormatTest, FastClear) {
  ASSERT_TRUE(Initialize());
  // This test reuqests a sysmem image with linear tiling and color attachment
  // usage, which is not supported by FEMU. So we skip this test on FEMU.
  //
  // TODO(fxbug.com/100837): Instead of skipping the test on specific platforms,
  // we should check if the features needed (i.e. tiled image of specific
  // formats, or linear image with some specific usages) are supported by all
  // the sysmem clients. Sysmem should send better error messages and we could
  // use this to determine if the test should be skipped due to unsupported
  // platforms.
  if (UseVirtualGpu()) {
    GTEST_SKIP();
  }

  constexpr bool kUseProtectedMemory = false;
  constexpr bool kUseLinear = false;
  constexpr uint32_t kPattern = 0xaabbccdd;

  const VkFormat format = GetParam();

  vk::UniqueImage image;
  vk::UniqueDeviceMemory memory;

  fuchsia::sysmem::BufferCollectionInfo_2 sysmem_collection;
  bool src_is_coherent;
  {
    auto [vulkan_token, local_token] = MakeSharedCollection<2>();

    vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, format, kDefaultWidth, kDefaultHeight, kUseLinear);
    image_create_info.setUsage(vk::ImageUsageFlagBits::eColorAttachment |
                               vk::ImageUsageFlagBits::eTransferDst);
    image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.requiredFormatFeatures |= vk::FormatFeatureFlagBits::eColorAttachment;
    format_constraints.imageCreateInfo = image_create_info;

    UniqueBufferCollection collection = CreateVkBufferCollectionForImage(
        std::move(vulkan_token), format_constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
            vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageRead;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.cpu_domain_supported = true;
    constraints.buffer_memory_constraints.ram_domain_supported = true;

    constraints.image_format_constraints_count = 2;
    {
      // Intel needs Y or YF tiling to do a fast clear.
      auto &image_constraints = constraints.image_format_constraints[0];
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
      image_constraints.pixel_format.has_format_modifier = true;
      image_constraints.pixel_format.format_modifier.value =
          fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED;
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
    }
    {
      auto &image_constraints = constraints.image_format_constraints[1];
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
      image_constraints.pixel_format.has_format_modifier = true;
      image_constraints.pixel_format.format_modifier.value =
          fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
    }

    sysmem_collection = AllocateSysmemCollection(constraints, std::move(local_token));

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

    std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection);
    ASSERT_TRUE(init_img_memory_result);
    uint32_t memoryTypeIndex = init_img_memory_result.value();
    src_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

    image = std::move(vk_image_);
    memory = std::move(vk_device_memory_);

    WriteLinearImage(memory.get(), src_is_coherent, kDefaultWidth, kDefaultHeight, kPattern);
  }

  vk::UniqueCommandPool command_pool;
  {
    auto info =
        vk::CommandPoolCreateInfo().setQueueFamilyIndex(vulkan_context().queue_family_index());
    auto result = vulkan_context().device()->createCommandPoolUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_pool = std::move(result.value);
  }

  std::vector<vk::UniqueCommandBuffer> command_buffers;
  {
    auto info = vk::CommandBufferAllocateInfo()
                    .setCommandPool(command_pool.get())
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(1);
    auto result = vulkan_context().device()->allocateCommandBuffersUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_buffers = std::move(result.value);
  }

  {
    auto info = vk::CommandBufferBeginInfo();
    EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->begin(&info));
  }

  vk::UniqueRenderPass render_pass;
  {
    std::array<vk::AttachmentDescription, 1> attachments;
    auto &color_attachment = attachments[0];
    color_attachment.format = static_cast<vk::Format>(format);
    color_attachment.initialLayout = vk::ImageLayout::ePreinitialized;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;
    vk::SubpassDescription subpass;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

    vk::RenderPassCreateInfo render_pass_info;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.subpassCount = 1;
    auto result = vulkan_context().device()->createRenderPassUnique(render_pass_info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    render_pass = std::move(result.value);
  }
  vk::UniqueImageView image_view;
  {
    vk::ImageSubresourceRange range;
    range.aspectMask = vk::ImageAspectFlagBits::eColor;
    range.layerCount = 1;
    range.levelCount = 1;
    vk::ImageViewCreateInfo info;
    info.image = *image;
    info.viewType = vk::ImageViewType::e2D;
    info.format = static_cast<vk::Format>(format);
    info.subresourceRange = range;

    auto result = vulkan_context().device()->createImageViewUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    image_view = std::move(result.value);
  }
  vk::UniqueFramebuffer frame_buffer;
  {
    vk::FramebufferCreateInfo create_info;
    create_info.renderPass = *render_pass;
    create_info.attachmentCount = 1;
    std::array<vk::ImageView, 1> attachments{*image_view};
    create_info.setAttachments(attachments);
    create_info.width = kDefaultWidth;
    create_info.height = kDefaultHeight;
    create_info.layers = 1;
    auto result = vulkan_context().device()->createFramebufferUnique(create_info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    frame_buffer = std::move(result.value);
  }

  vk::RenderPassBeginInfo render_pass_info;
  vk::ClearValue clear_color;
  clear_color.color = std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f};
  render_pass_info.renderPass = *render_pass;
  render_pass_info.renderArea =
      vk::Rect2D(0 /* offset */, vk::Extent2D(kDefaultWidth, kDefaultHeight));
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;
  render_pass_info.framebuffer = *frame_buffer;

  // Clears and stores the framebuffer.
  command_buffers[0]->beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
  command_buffers[0]->endRenderPass();

  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    // TODO(fxbug.dev/93236): Test transitioning to
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. That's broken with SRGB on the
    // current version of Mesa.
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead |
                                         vk::AccessFlagBits::eColorAttachmentWrite)
                       .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                       .setNewLayout(vk::ImageLayout::eGeneral)
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_FOREIGN_EXT)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput, /* srcStageMask */
        vk::PipelineStageFlagBits::eColorAttachmentOutput, /* dstStageMask */
        vk::DependencyFlagBits::eByRegion, 0 /* memoryBarrierCount */,
        nullptr /* pMemoryBarriers */, 0 /* bufferMemoryBarrierCount */,
        nullptr /* pBufferMemoryBarriers */, 1 /* imageMemoryBarrierCount */, &barrier);
  }

  EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->end());

  {
    auto command_buffer_temp = command_buffers[0].get();
    auto info = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&command_buffer_temp);
    EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().submit(1, &info, vk::Fence()));
  }

  EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().waitIdle());

  ASSERT_TRUE(sysmem_collection.settings.has_image_format_constraints);
  {
    void *addr;
    vk::Result result = ctx_->device()->mapMemory(*memory, 0 /* offset */, VK_WHOLE_SIZE,
                                                  vk::MemoryMapFlags{}, &addr);
    ASSERT_EQ(vk::Result::eSuccess, result);

    if (!src_is_coherent) {
      auto range = vk::MappedMemoryRange().setMemory(*memory).setSize(VK_WHOLE_SIZE);
      EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->invalidateMappedMemoryRanges(1, &range));
    }

    CheckImageFill(kDefaultWidth, kDefaultHeight, addr, sysmem_collection, 0xffffffff);
    ctx_->device()->unmapMemory(*memory);
  }
}

// Test on UNORM and SRGB, because on older Intel devices UNORM supports CCS_E, but SRGB only
// supports CCS_D.
INSTANTIATE_TEST_SUITE_P(, VulkanFormatTest,
                         ::testing::Values(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB),
                         [](const testing::TestParamInfo<VulkanFormatTest::ParamType> &info) {
                           return vk::to_string(static_cast<vk::Format>(info.param));
                         });

// Test copying through an optimal format, including importing images at a
// smaller size than the constraints set on the buffer collection.
TEST_F(VulkanExtensionTest, OptimalCopy) {
  ASSERT_TRUE(Initialize());

  constexpr bool kUseProtectedMemory = false;
  constexpr uint32_t kPattern = 0xaabbccdd;

  vk::UniqueImage src_image;
  vk::UniqueDeviceMemory src_memory;
  bool src_is_coherent;

  {
    auto [vulkan_token] = MakeSharedCollection<1>();
    constexpr bool kUseLinear = true;

    vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, kDefaultFormat, kDefaultWidth, kDefaultHeight, kUseLinear);
    image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
    image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = image_create_info;

    UniqueBufferCollection collection = CreateVkBufferCollectionForImage(
        std::move(vulkan_token), format_constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
            vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

    std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection);
    ASSERT_TRUE(init_img_memory_result);
    uint32_t memoryTypeIndex = init_img_memory_result.value();
    src_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

    src_image = std::move(vk_image_);
    src_memory = std::move(vk_device_memory_);

    WriteLinearImage(src_memory.get(), src_is_coherent, kDefaultWidth, kDefaultHeight, kPattern);
  }

  vk::UniqueImage mid_image1, mid_image2;
  vk::UniqueDeviceMemory mid_memory1, mid_memory2;

  // Create a buffer collection and import it twice, once as mid_image1 and once
  // as mid_image2. The two different VkBufferCollections will have different
  // (larger) size constraints then the images.
  {
    auto [vulkan_token1, vulkan_token2] = MakeSharedCollection<2>();
    constexpr bool kUseLinear = false;
    UniqueBufferCollection collection1;
    UniqueBufferCollection collection2;

    {
      vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
          kUseProtectedMemory, kDefaultFormat, kDefaultWidth * 2, kDefaultHeight * 2, kUseLinear);
      image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferDst);
      image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

      vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
          GetDefaultRgbImageFormatConstraintsInfo();
      format_constraints.imageCreateInfo = image_create_info;

      collection1 = CreateVkBufferCollectionForImage(
          std::move(vulkan_token1), format_constraints,
          vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
              vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);
    }

    {
      vk::ImageCreateInfo image_create_info =
          GetDefaultImageCreateInfo(kUseProtectedMemory, kDefaultFormat, kDefaultWidth * 3 / 2,
                                    kDefaultHeight * 3 / 2, kUseLinear);
      image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
      image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

      vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
          GetDefaultRgbImageFormatConstraintsInfo();
      format_constraints.imageCreateInfo = image_create_info;

      collection2 = CreateVkBufferCollectionForImage(
          std::move(vulkan_token2), format_constraints,
          vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
              vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);
    }

    vk::ImageCreateInfo real_image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, kDefaultFormat, kDefaultWidth, kDefaultHeight, kUseLinear);
    real_image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferDst);
    real_image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);
    {
      ASSERT_TRUE(InitializeDirectImage(*collection1, real_image_create_info));

      std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection1);
      ASSERT_TRUE(init_img_memory_result);
      uint32_t memoryTypeIndex = init_img_memory_result.value();
      bool mid_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

      mid_image1 = std::move(vk_image_);
      mid_memory1 = std::move(vk_device_memory_);

      WriteLinearImage(mid_memory1.get(), mid_is_coherent, kDefaultWidth, kDefaultHeight,
                       0xffffffff);
    }
    {
      real_image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferSrc);
      ASSERT_TRUE(InitializeDirectImage(*collection2, real_image_create_info));

      std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection1);
      ASSERT_TRUE(init_img_memory_result);

      mid_image2 = std::move(vk_image_);
      mid_memory2 = std::move(vk_device_memory_);
    }
  }

  vk::UniqueImage dst_image;
  vk::UniqueDeviceMemory dst_memory;
  bool dst_is_coherent;

  {
    auto [vulkan_token] = MakeSharedCollection<1>();
    constexpr bool kUseLinear = true;

    vk::ImageCreateInfo image_create_info = GetDefaultImageCreateInfo(
        kUseProtectedMemory, kDefaultFormat, kDefaultWidth, kDefaultHeight, kUseLinear);
    image_create_info.setUsage(vk::ImageUsageFlagBits::eTransferDst);
    image_create_info.setInitialLayout(vk::ImageLayout::ePreinitialized);

    vk::ImageFormatConstraintsInfoFUCHSIA format_constraints =
        GetDefaultRgbImageFormatConstraintsInfo();
    format_constraints.imageCreateInfo = image_create_info;

    UniqueBufferCollection collection = CreateVkBufferCollectionForImage(
        std::move(vulkan_token), format_constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuReadOften |
            vk::ImageConstraintsInfoFlagBitsFUCHSIA::eCpuWriteOften);

    ASSERT_TRUE(InitializeDirectImage(*collection, image_create_info));

    std::optional<uint32_t> init_img_memory_result = InitializeDirectImageMemory(*collection);
    ASSERT_TRUE(init_img_memory_result);
    uint32_t memoryTypeIndex = init_img_memory_result.value();
    dst_is_coherent = IsMemoryTypeCoherent(memoryTypeIndex);

    dst_image = std::move(vk_image_);
    dst_memory = std::move(vk_device_memory_);

    WriteLinearImage(dst_memory.get(), dst_is_coherent, kDefaultWidth, kDefaultHeight, 0xffffffff);
  }

  auto range = vk::ImageSubresourceRange()
                   .setAspectMask(vk::ImageAspectFlagBits::eColor)
                   .setLevelCount(1)
                   .setLayerCount(1);
  auto layer =
      vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1);
  vk::UniqueCommandPool command_pool;
  {
    auto info =
        vk::CommandPoolCreateInfo().setQueueFamilyIndex(vulkan_context().queue_family_index());
    auto result = vulkan_context().device()->createCommandPoolUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_pool = std::move(result.value);
  }

  std::vector<vk::UniqueCommandBuffer> command_buffers;
  {
    auto info = vk::CommandBufferAllocateInfo()
                    .setCommandPool(command_pool.get())
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(1);
    auto result = vulkan_context().device()->allocateCommandBuffersUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_buffers = std::move(result.value);
  }

  {
    auto info = vk::CommandBufferBeginInfo();
    EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->begin(&info));
  }

  // transition src_image to be readable by transfer.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(src_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  // transition mid_image1 to be readable by transfer.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(mid_image1.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  {
    auto copy = vk::ImageCopy()
                    .setSrcSubresource(layer)
                    .setDstSubresource(layer)
                    .setSrcOffset({0, 0, 0})
                    .setDstOffset({0, 0, 0})
                    .setExtent({kDefaultWidth, kDefaultHeight, 1});
    command_buffers[0]->copyImage(src_image.get(), vk::ImageLayout::eTransferSrcOptimal,
                                  mid_image1.get(), vk::ImageLayout::eTransferDstOptimal, copy);
  }
  // Do a transfer of mid_image1 to the foreign queue family.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(mid_image1.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setDstAccessMask({})
                       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSrcQueueFamilyIndex(ctx_->queue_family_index())
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_FOREIGN_EXT)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  // Do a transfer of mid_image2 to the foreign queue family.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(mid_image2.get())
                       .setSrcAccessMask({})
                       .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                       .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_FOREIGN_EXT)
                       .setDstQueueFamilyIndex(ctx_->queue_family_index())
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  // Transition dst_image to be writable by transfer stage.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  {
    auto copy2 = vk::ImageCopy()
                     .setSrcSubresource(layer)
                     .setDstSubresource(layer)
                     .setSrcOffset({0, 0, 0})
                     .setDstOffset({0, 0, 0})
                     .setExtent({kDefaultWidth, kDefaultHeight, 1});
    command_buffers[0]->copyImage(mid_image2.get(), vk::ImageLayout::eTransferSrcOptimal,
                                  dst_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copy2);
  }
  // Transition dst image to be readable on the CPU.
  {
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eHostRead)
                       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setNewLayout(vk::ImageLayout::eGeneral)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, /* srcStageMask */
        vk::PipelineStageFlagBits::eHost,     /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->end());

  {
    auto command_buffer_temp = command_buffers[0].get();
    auto info = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&command_buffer_temp);
    EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().submit(1, &info, vk::Fence()));
  }

  EXPECT_EQ(vk::Result::eSuccess, vulkan_context().queue().waitIdle());

  CheckLinearImage(dst_memory.get(), dst_is_coherent, kDefaultWidth, kDefaultHeight, kPattern);
}

}  // namespace
