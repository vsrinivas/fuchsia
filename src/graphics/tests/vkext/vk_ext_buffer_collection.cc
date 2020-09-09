// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace {

vk::ImageCreateInfo GetDefaultImageCreateInfo(bool use_protected_memory, VkFormat format,
                                              uint32_t width, uint32_t height, bool linear) {
  return vk::ImageCreateInfo()
      .setFlags(use_protected_memory ? vk::ImageCreateFlagBits::eProtected
                                     : vk::ImageCreateFlagBits())
      .setImageType(vk::ImageType::e2D)
      .setFormat(vk::Format(format))
      .setExtent(vk::Extent3D(width, height, 1))
      .setMipLevels(1)
      .setArrayLayers(1)
      .setSamples(vk::SampleCountFlagBits::e1)
      .setTiling(linear ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal)
      // Only use sampled, because on Mali some other usages (like color attachment) aren't
      // supported for NV12, and some others (implementation-dependent) aren't supported with
      // AFBC.
      .setUsage(vk::ImageUsageFlagBits::eSampled)
      .setSharingMode(vk::SharingMode::eExclusive)
      .setInitialLayout(vk::ImageLayout::eUndefined);
}

fuchsia::sysmem::ImageFormatConstraints GetDefaultSysmemImageFormatConstraints() {
  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints;
  bgra_image_constraints.required_min_coded_width = 1024;
  bgra_image_constraints.required_min_coded_height = 1024;
  bgra_image_constraints.required_max_coded_width = 1024;
  bgra_image_constraints.required_max_coded_height = 1024;
  bgra_image_constraints.max_coded_width = 8192;
  bgra_image_constraints.max_coded_height = 8192;
  bgra_image_constraints.max_bytes_per_row = 0xffffffff;
  bgra_image_constraints.pixel_format = {fuchsia::sysmem::PixelFormatType::BGRA32, false};
  bgra_image_constraints.color_spaces_count = 1;
  bgra_image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
  return bgra_image_constraints;
}

class VulkanExtensionTest : public testing::Test {
 public:
  ~VulkanExtensionTest();
  bool Initialize();
  bool Exec(VkFormat format, uint32_t width, uint32_t height, bool direct, bool linear,
            bool repeat_constraints_as_non_protected,
            const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints =
                std::vector<fuchsia::sysmem::ImageFormatConstraints>());
  bool ExecBuffer(uint32_t size);

  void set_use_protected_memory(bool use) { use_protected_memory_ = use; }
  bool device_supports_protected_memory() const { return device_supports_protected_memory_; }

 private:
  using UniqueBufferCollection =
      vk::UniqueHandle<vk::BufferCollectionFUCHSIA, vk::DispatchLoaderDynamic>;

  bool InitVulkan();
  bool InitSysmemAllocator();
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> MakeSharedCollection(
      uint32_t token_count);
  UniqueBufferCollection CreateVkBufferCollectionForImage(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token, vk::ImageCreateInfo image_create_info);
  fuchsia::sysmem::BufferCollectionInfo_2 AllocateSysmemCollection(
      std::optional<fuchsia::sysmem::BufferCollectionConstraints> constraints,
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token);
  void InitializeNonDirectImage(fuchsia::sysmem::BufferCollectionInfo_2 &buffer_collection_info,
                                VkImageCreateInfo image_create_info);
  void InitializeNonDirectMemory(fuchsia::sysmem::BufferCollectionInfo_2 &buffer_collection_info);
  void InitializeDirectImage(vk::BufferCollectionFUCHSIA collection,
                             vk::ImageCreateInfo image_create_info);
  void InitializeDirectImageMemory(vk::BufferCollectionFUCHSIA collection);
  void CheckLinearSubresourceLayout(VkFormat format, uint32_t width);
  void ValidateBufferProperties(const VkMemoryRequirements &requirements,
                                const vk::BufferCollectionFUCHSIA collection,
                                uint32_t expected_count, uint32_t *memory_type_out);

  bool is_initialized_ = false;
  bool use_protected_memory_ = false;
  bool device_supports_protected_memory_ = false;
  std::unique_ptr<VulkanContext> ctx_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  vk::UniqueImage vk_image_;
  VkDeviceMemory vk_device_memory_{};
  vk::DispatchLoaderDynamic loader_;
};

VulkanExtensionTest::~VulkanExtensionTest() {
  const vk::Device &device = *ctx_->device();
  if (vk_device_memory_) {
    vkFreeMemory(device, vk_device_memory_, nullptr);
    vk_device_memory_ = VK_NULL_HANDLE;
  }
}

bool VulkanExtensionTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan()) {
    RTN_MSG(false, "InitVulkan failed.\n");
  }

  if (!InitSysmemAllocator()) {
    RTN_MSG(false, "InitSysmemAllocator failed.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VulkanExtensionTest::InitVulkan() {
  constexpr size_t kPhysicalDeviceIndex = 0;
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkext";
  app_info.apiVersion = VK_API_VERSION_1_1;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;
  ctx_ = std::make_unique<VulkanContext>(kPhysicalDeviceIndex);
  ctx_->set_instance_info(instance_info);
  if (!ctx_->InitInstance()) {
    return false;
  }

  loader_.init(*ctx_->instance(), vkGetInstanceProcAddr);
  if (!ctx_->InitQueueFamily()) {
    return false;
  }

  // Set |device_supports_protected_memory_| flag.
  vk::PhysicalDeviceProtectedMemoryFeatures protected_memory(VK_TRUE);
  vk::PhysicalDeviceProperties physical_device_properties;
  ctx_->physical_device().getProperties(&physical_device_properties);
  if (VK_VERSION_MAJOR(physical_device_properties.apiVersion) != 1 ||
      VK_VERSION_MINOR(physical_device_properties.apiVersion) > 0) {
    vk::PhysicalDeviceFeatures2 features2;
    features2.pNext = &protected_memory;
    ctx_->physical_device().getFeatures2(&features2);
    if (protected_memory.protectedMemory) {
      device_supports_protected_memory_ = true;
    }
  }

  std::vector<const char *> enabled_device_extensions{VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME};
  vk::DeviceCreateInfo device_info;
  device_info.pNext = device_supports_protected_memory_ ? &protected_memory : nullptr;
  device_info.pQueueCreateInfos = &ctx_->queue_info();
  device_info.queueCreateInfoCount = 1;
  device_info.enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size());
  device_info.ppEnabledExtensionNames = enabled_device_extensions.data();

  ctx_->set_device_info(device_info);
  if (!ctx_->InitDevice()) {
    return false;
  }

  return true;
}

bool VulkanExtensionTest::InitSysmemAllocator() {
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    RTN_MSG(false, "Fdio_service_connect failed: %d\n", status);
  }
  return true;
}

std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>
VulkanExtensionTest::MakeSharedCollection(uint32_t token_count) {
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> tokens;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token1;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token1.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  for (uint32_t i = 1; i < token_count; ++i) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr tokenN;
    status = token1->Duplicate(std::numeric_limits<uint32_t>::max(), tokenN.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    tokens.push_back(std::move(tokenN));
  }

  status = token1->Sync();
  EXPECT_EQ(ZX_OK, status);
  tokens.push_back(std::move(token1));
  return tokens;
}

void VulkanExtensionTest::CheckLinearSubresourceLayout(VkFormat format, uint32_t width) {
  const vk::Device &device = *ctx_->device();
  bool is_yuv = (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) ||
                (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR);
  VkImageSubresource subresource = {
      .aspectMask = is_yuv ? VK_IMAGE_ASPECT_PLANE_0_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .arrayLayer = 0};
  VkSubresourceLayout layout;
  vkGetImageSubresourceLayout(device, *vk_image_, &subresource, &layout);

  VkDeviceSize min_bytes_per_pixel = is_yuv ? 1 : 4;
  EXPECT_LE(min_bytes_per_pixel * width, layout.rowPitch);
  EXPECT_LE(min_bytes_per_pixel * width * 64, layout.size);

  if (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR) {
    VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT, .mipLevel = 0, .arrayLayer = 0};
    VkSubresourceLayout b_layout;
    vkGetImageSubresourceLayout(device, *vk_image_, &subresource, &b_layout);

    subresource.aspectMask = VK_IMAGE_ASPECT_PLANE_2_BIT;
    VkSubresourceLayout r_layout;
    vkGetImageSubresourceLayout(device, *vk_image_, &subresource, &r_layout);

    // I420 has the U plane (mapped to B) before the V plane (mapped to R)
    EXPECT_LT(b_layout.offset, r_layout.offset);
  }
}

void VulkanExtensionTest::ValidateBufferProperties(const VkMemoryRequirements &requirements,
                                                   const vk::BufferCollectionFUCHSIA collection,
                                                   uint32_t expected_count,
                                                   uint32_t *memory_type_out) {
  vk::BufferCollectionPropertiesFUCHSIA properties;
  vk::Result result1 =
      ctx_->device()->getBufferCollectionPropertiesFUCHSIA(collection, &properties, loader_);
  EXPECT_EQ(result1, vk::Result::eSuccess);

  EXPECT_EQ(expected_count, properties.count);
  uint32_t viable_memory_types = properties.memoryTypeBits & requirements.memoryTypeBits;
  EXPECT_NE(0u, viable_memory_types);
  uint32_t memory_type = __builtin_ctz(viable_memory_types);

  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_properties);

  EXPECT_LT(memory_type, memory_properties.memoryTypeCount);
  if (use_protected_memory_) {
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
      if (properties.memoryTypeBits & (1 << i)) {
        // Based only on the buffer collection it should be possible to
        // determine that this is protected memory. viable_memory_types
        // is a subset of these bits, so that should be true for it as
        // well.
        EXPECT_TRUE(memory_properties.memoryTypes[i].propertyFlags &
                    VK_MEMORY_PROPERTY_PROTECTED_BIT);
      }
    }
  } else {
    EXPECT_FALSE(memory_properties.memoryTypes[memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_PROTECTED_BIT);
  }
  *memory_type_out = memory_type;
}

fuchsia::sysmem::BufferCollectionInfo_2 VulkanExtensionTest::AllocateSysmemCollection(
    std::optional<fuchsia::sysmem::BufferCollectionConstraints> constraints,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token) {
  fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection;
  zx_status_t status =
      sysmem_allocator_->BindSharedCollection(std::move(token), sysmem_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  if (constraints) {
    EXPECT_EQ(ZX_OK, sysmem_collection->SetConstraints(true, *constraints));
  } else {
    EXPECT_EQ(ZX_OK, sysmem_collection->SetConstraints(
                         false, fuchsia::sysmem::BufferCollectionConstraints()));
  }

  zx_status_t allocation_status;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  EXPECT_EQ(ZX_OK, sysmem_collection->WaitForBuffersAllocated(&allocation_status,
                                                              &buffer_collection_info));
  EXPECT_EQ(ZX_OK, allocation_status);
  EXPECT_EQ(ZX_OK, sysmem_collection->Close());
  return buffer_collection_info;
}

void VulkanExtensionTest::InitializeNonDirectImage(
    fuchsia::sysmem::BufferCollectionInfo_2 &buffer_collection_info,
    VkImageCreateInfo image_create_info) {
  fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
  encoder.Alloc(fidl::EncodingInlineSize<fuchsia::sysmem::SingleBufferSettings>(&encoder));
  buffer_collection_info.settings.Encode(&encoder, 0);
  std::vector<uint8_t> encoded_data = encoder.TakeBytes();

  VkFuchsiaImageFormatFUCHSIA image_format_fuchsia = {
      .sType = VK_STRUCTURE_TYPE_FUCHSIA_IMAGE_FORMAT_FUCHSIA,
      .pNext = nullptr,
      .imageFormat = encoded_data.data(),
      .imageFormatSize = static_cast<uint32_t>(encoded_data.size())};
  image_create_info.pNext = &image_format_fuchsia;

  auto [result, vk_image] = ctx_->device()->createImageUnique(image_create_info, nullptr);
  EXPECT_EQ(vk::Result::eSuccess, result);
  vk_image_ = std::move(vk_image);
}

void VulkanExtensionTest::InitializeNonDirectMemory(
    fuchsia::sysmem::BufferCollectionInfo_2 &buffer_collection_info) {
  const vk::Device &device = *ctx_->device();
  VkMemoryRequirements memory_reqs;
  vkGetImageMemoryRequirements(device, *vk_image_, &memory_reqs);
  // Use first supported type
  uint32_t memory_type = __builtin_ctz(memory_reqs.memoryTypeBits);

  // The driver may not have the right information to choose the correct
  // heap for protected memory.
  EXPECT_FALSE(use_protected_memory_);

  VkImportMemoryZirconHandleInfoFUCHSIA handle_info = {
      .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA,
      .pNext = nullptr,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
      buffer_collection_info.buffers[0].vmo.release()};

  VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &handle_info,
      .allocationSize = memory_reqs.size,
      .memoryTypeIndex = memory_type,
  };
  VkResult result;
  if ((result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_)) != VK_SUCCESS) {
    ASSERT_TRUE(false);
  }

  result = vkBindImageMemory(device, *vk_image_, vk_device_memory_, 0);
  if (result != VK_SUCCESS) {
    ASSERT_TRUE(false);
  }
}

void VulkanExtensionTest::InitializeDirectImage(vk::BufferCollectionFUCHSIA collection,
                                                vk::ImageCreateInfo image_create_info) {
  VkBufferCollectionImageCreateInfoFUCHSIA image_format_fuchsia = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collection = collection,
      .index = 0};
  if (image_create_info.format == vk::Format::eUndefined) {
    // Ensure that the image created matches what was asked for on
    // sysmem_connection.
    image_create_info.extent.width = 1024;
    image_create_info.extent.height = 1024;
    image_create_info.format = vk::Format::eB8G8R8A8Unorm;
  }
  image_create_info.pNext = &image_format_fuchsia;

  auto [result, vk_image] = ctx_->device()->createImageUnique(image_create_info, nullptr);
  EXPECT_EQ(result, vk::Result::eSuccess);
  vk_image_ = std::move(vk_image);
}

void VulkanExtensionTest::InitializeDirectImageMemory(vk::BufferCollectionFUCHSIA collection) {
  const vk::Device &device = *ctx_->device();
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(device, *vk_image_, &requirements);
  uint32_t memory_type;
  ValidateBufferProperties(requirements, collection, 1u, &memory_type);

  VkImportMemoryBufferCollectionFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA};

  import_info.collection = collection;
  import_info.index = 0;
  VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc_info.pNext = &import_info;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = memory_type;

  VkResult result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_);
  EXPECT_EQ(VK_SUCCESS, result);

  result = vkBindImageMemory(device, *vk_image_, vk_device_memory_, 0u);
  EXPECT_EQ(VK_SUCCESS, result);
}

VulkanExtensionTest::UniqueBufferCollection VulkanExtensionTest::CreateVkBufferCollectionForImage(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, vk::ImageCreateInfo image_create_info) {
  vk::BufferCollectionCreateInfoFUCHSIA import_info(token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);
  result = ctx_->device()->setBufferCollectionConstraintsFUCHSIA(*collection, &image_create_info,
                                                                 loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);
  return std::move(collection);
}

bool VulkanExtensionTest::Exec(
    VkFormat format, uint32_t width, uint32_t height, bool direct, bool linear,
    bool repeat_constraints_as_non_protected,
    const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints) {
  auto tokens = MakeSharedCollection(3u);
  auto local_token = std::move(tokens[0]);
  auto vulkan_token = std::move(tokens[1]);
  auto non_protected_token = std::move(tokens[2]);

  // This bool suggests that we dup another token to set the same constraints, skipping protected
  // memory requirements. This emulates another participant which does not require protected memory.
  UniqueBufferCollection non_protected_collection;
  if (repeat_constraints_as_non_protected) {
    auto image_create_info =
        GetDefaultImageCreateInfo(/*use_protected_memory=*/false, format, width, height, linear);
    non_protected_collection =
        CreateVkBufferCollectionForImage(std::move(non_protected_token), image_create_info);
  } else {
    // Close the token to prevent sysmem from waiting on it.
    non_protected_token->Close();
    non_protected_token = {};
  }

  auto image_create_info =
      GetDefaultImageCreateInfo(use_protected_memory_, format, width, height, linear);
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), image_create_info);

  std::optional<fuchsia::sysmem::BufferCollectionConstraints> constraints_option;
  if (!format_constraints.empty()) {
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    // Use the other connection to specify the actual desired format and size,
    // which should be compatible with what the vulkan driver can use.
    assert(direct);
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    // Try multiple format modifiers.
    constraints.image_format_constraints_count = format_constraints.size();
    for (uint32_t i = 0; i < constraints.image_format_constraints_count; i++) {
      constraints.image_format_constraints[i] = format_constraints[i];
    }
    constraints_option = constraints;
  } else if (direct) {
  } else {
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    // The total buffer count should be 1 with or without this set (because
    // the Vulkan driver sets a minimum of one buffer).
    constraints.min_buffer_count_for_camping = 1;
    constraints_option = constraints;
  }
  auto buffer_collection_info =
      AllocateSysmemCollection(constraints_option, std::move(local_token));

  EXPECT_EQ(1u, buffer_collection_info.buffer_count);
  fuchsia::sysmem::PixelFormat pixel_format =
      buffer_collection_info.settings.image_format_constraints.pixel_format;

  if (format == VK_FORMAT_UNDEFINED && direct) {
    EXPECT_EQ(pixel_format.type, fuchsia::sysmem::PixelFormatType::BGRA32);
  }

  if (!direct) {
    InitializeNonDirectImage(buffer_collection_info, image_create_info);
  } else {
    InitializeDirectImage(*collection, image_create_info);
  }

  if (linear) {
    CheckLinearSubresourceLayout(format, width);
  }

  if (!direct) {
    InitializeNonDirectMemory(buffer_collection_info);
  } else {
    InitializeDirectImageMemory(*collection);
  }

  return true;
}

bool VulkanExtensionTest::ExecBuffer(uint32_t size) {
  VkResult result;
  const vk::Device &device = *ctx_->device();

  auto tokens = MakeSharedCollection(2);
  auto local_token = std::move(tokens[0]);
  auto vulkan_token = std::move(tokens[1]);

  constexpr uint32_t kMinBufferCount = 2;

  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.flags =
      use_protected_memory_ ? vk::BufferCreateFlagBits::eProtected : vk::BufferCreateFlagBits();
  buffer_create_info.size = size;
  buffer_create_info.usage = vk::BufferUsageFlagBits::eIndexBuffer;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

  vk::BufferCollectionCreateInfoFUCHSIA import_info(vulkan_token.Unbind().TakeChannel().release());
  vk::BufferCollectionFUCHSIA collection;
  vk::Result result1 =
      ctx_->device()->createBufferCollectionFUCHSIA(&import_info, nullptr, &collection, loader_);
  if (result1 != vk::Result::eSuccess) {
    RTN_MSG(false, "Failed to create buffer collection: %d\n", result1);
  }

  vk::BufferConstraintsInfoFUCHSIA constraints;
  constraints.pBufferCreateInfo = &buffer_create_info;
  constraints.requiredFormatFeatures = vk::FormatFeatureFlagBits::eVertexBuffer;
  constraints.minCount = kMinBufferCount;

  result1 =
      ctx_->device()->setBufferCollectionBufferConstraintsFUCHSIA(collection, constraints, loader_);

  if (result1 != vk::Result::eSuccess) {
    RTN_MSG(false, "Failed to set buffer constraints: %d\n", result1);
  }

  auto buffer_collection_info = AllocateSysmemCollection({}, std::move(local_token));

  VkBufferCollectionBufferCreateInfoFUCHSIA collection_buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_BUFFER_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collection = collection,
      .index = 1};
  buffer_create_info.pNext = &collection_buffer_create_info;

  VkBuffer buffer;

  result = vkCreateBuffer(device, &static_cast<const VkBufferCreateInfo &>(buffer_create_info),
                          nullptr, &buffer);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateBuffer failed: %d\n", result);
  }

  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  uint32_t memory_type;
  ValidateBufferProperties(requirements, collection, kMinBufferCount, &memory_type);

  VkImportMemoryBufferCollectionFUCHSIA memory_import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
      .collection = collection,
      .index = 1};

  VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc_info.pNext = &memory_import_info;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = memory_type;

  result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  result = vkBindBufferMemory(device, buffer, vk_device_memory_, 0u);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  vkDestroyBuffer(device, buffer, nullptr);

  ctx_->device()->destroyBufferCollectionFUCHSIA(collection, nullptr, loader_);
  return true;
}

// Parameter is true if the image should be linear.
class VulkanImageExtensionTest : public VulkanExtensionTest,
                                 public ::testing::WithParamInterface<bool> {};

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionI420) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12_1025) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 1025, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA_1025) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 1025, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectNV12) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectI420) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectNV12_1280_546) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 8192, 546, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionUndefined) {
  ASSERT_TRUE(Initialize());

  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  fuchsia::sysmem::ImageFormatConstraints bgra_tiled_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  bgra_tiled_image_constraints.pixel_format = {
      fuchsia::sysmem::PixelFormatType::BGRA32,
      true,
      {fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED}};
  std::vector<fuchsia::sysmem::ImageFormatConstraints> two_constraints{
      bgra_image_constraints, bgra_tiled_image_constraints};

  ASSERT_TRUE(Exec(VK_FORMAT_UNDEFINED, 64, 64, true, GetParam(), false, two_constraints));
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

  ASSERT_TRUE(
      Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, true, GetParam(), false, all_constraints));
  ASSERT_TRUE(Exec(VK_FORMAT_B8G8R8A8_UNORM, 64, 64, true, GetParam(), false, all_constraints));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionProtectedRGBA) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, ProtectedAndNonprotectedConstraints) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, true, GetParam(), true));
}

INSTANTIATE_TEST_SUITE_P(, VulkanImageExtensionTest, ::testing::Bool());

TEST_F(VulkanExtensionTest, BufferCollectionBuffer1024) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ExecBuffer(1024));
}

TEST_F(VulkanExtensionTest, BufferCollectionBuffer16384) {
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ExecBuffer(16384));
}

TEST_F(VulkanExtensionTest, BufferCollectionProtectedBuffer) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
  ASSERT_TRUE(ExecBuffer(16384));
}

}  // namespace
