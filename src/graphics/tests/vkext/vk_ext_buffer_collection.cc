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

#include "vulkan/vulkan_enums.hpp"
#include <vulkan/vulkan.hpp>

namespace {

constexpr uint32_t kDefaultWidth = 64;
constexpr uint32_t kDefaultHeight = 64;
constexpr VkFormat kDefaultFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDefaultYuvFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
constexpr vk::SysmemColorSpaceFUCHSIA kDefaultRgbColorSpace(
    static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::SRGB));
constexpr vk::SysmemColorSpaceFUCHSIA kDefaultYuvColorSpace(
    static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709));

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
      // Only use TransferDst, because on Mali some other usages (like color attachment) aren't
      // supported for NV12, and some others (implementation-dependent) aren't supported with
      // AFBC, and sampled aren't supported with SwiftShader (linear images).
      .setUsage(vk::ImageUsageFlagBits::eTransferDst)
      .setSharingMode(vk::SharingMode::eExclusive)
      .setInitialLayout(vk::ImageLayout::eUndefined);
}

vk::ImageFormatConstraintsInfoFUCHSIA GetDefaultImageFormatConstraintsInfo(bool yuv) {
  return vk::ImageFormatConstraintsInfoFUCHSIA()
      .setSysmemPixelFormat(0u)
      .setFlags({})
      .setPColorSpaces(yuv ? &kDefaultYuvColorSpace : &kDefaultRgbColorSpace)
      .setColorSpaceCount(1u)
      .setRequiredFormatFeatures(vk::FormatFeatureFlagBits::eTransferDst);
}

vk::ImageFormatConstraintsInfoFUCHSIA GetDefaultRgbImageFormatConstraintsInfo() {
  return GetDefaultImageFormatConstraintsInfo(false);
}

vk::ImageFormatConstraintsInfoFUCHSIA GetDefaultYuvImageFormatConstraintsInfo() {
  return GetDefaultImageFormatConstraintsInfo(true);
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
  bool Exec(VkFormat format, uint32_t width, uint32_t height, bool linear,
            bool repeat_constraints_as_non_protected,
            const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints =
                std::vector<fuchsia::sysmem::ImageFormatConstraints>());
  bool ExecBuffer(uint32_t size);

  void set_use_protected_memory(bool use) { use_protected_memory_ = use; }
  bool device_supports_protected_memory() const { return device_supports_protected_memory_; }

  bool UseVirtualGpu() {
    auto properties = ctx_->physical_device().getProperties();
    return properties.deviceType == vk::PhysicalDeviceType::eVirtualGpu;
  }

  VulkanContext &vulkan_context() { return *ctx_; }

  bool IsMemoryTypeCoherent(uint32_t memoryTypeIndex);
  void WriteLinearImage(vk::DeviceMemory memory, bool is_coherent, uint32_t width, uint32_t height,
                        uint32_t fill);
  void CheckLinearImage(vk::DeviceMemory memory, bool is_coherent, uint32_t width, uint32_t height,
                        uint32_t fill);

 protected:
  using UniqueBufferCollection =
      vk::UniqueHandle<vk::BufferCollectionFUCHSIA, vk::DispatchLoaderDynamic>;

  bool InitVulkan();
  bool InitSysmemAllocator();
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> MakeSharedCollection(
      uint32_t token_count);
  template <uint32_t token_count>
  std::array<fuchsia::sysmem::BufferCollectionTokenSyncPtr, token_count> MakeSharedCollection();
  UniqueBufferCollection CreateVkBufferCollectionForImage(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
      const vk::ImageFormatConstraintsInfoFUCHSIA constraints,
      vk::ImageConstraintsInfoFlagsFUCHSIA flags = {});
  fuchsia::sysmem::BufferCollectionInfo_2 AllocateSysmemCollection(
      std::optional<fuchsia::sysmem::BufferCollectionConstraints> constraints,
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token);
  bool InitializeDirectImage(vk::BufferCollectionFUCHSIA collection,
                             vk::ImageCreateInfo image_create_info);
  // Returns the memory type index if it succeeds; otherwise returns std::nullopt.
  std::optional<uint32_t> InitializeDirectImageMemory(vk::BufferCollectionFUCHSIA collection,
                                                      uint32_t expected_count = 1);
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
  vk::UniqueBuffer vk_buffer_;
  vk::UniqueDeviceMemory vk_device_memory_;
  vk::DispatchLoaderDynamic loader_;
};

VulkanExtensionTest::~VulkanExtensionTest() {}

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

  std::vector<const char *> enabled_device_extensions{VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
                                                      VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME};
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
  sysmem_allocator_->SetDebugClientInfo(fsl::GetCurrentProcessName(), fsl::GetCurrentProcessKoid());
  return true;
}

std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>
VulkanExtensionTest::MakeSharedCollection(uint32_t token_count) {
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> tokens;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token1;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token1.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  token1->SetName(1u, ::testing::UnitTest::GetInstance()->current_test_info()->name());

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

template <uint32_t token_count>
std::array<fuchsia::sysmem::BufferCollectionTokenSyncPtr, token_count>
VulkanExtensionTest::MakeSharedCollection() {
  auto token_vector = MakeSharedCollection(token_count);
  std::array<fuchsia::sysmem::BufferCollectionTokenSyncPtr, token_count> array;
  for (uint32_t i = 0; i < token_vector.size(); i++) {
    array[i] = std::move(token_vector[i]);
  }
  return array;
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

  VkDeviceSize min_bytes_per_pixel = 0;
  switch (format) {
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR:
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR:
      min_bytes_per_pixel = 1;
      break;

    case VK_FORMAT_R8_UNORM:
      min_bytes_per_pixel = 1;
      break;

    case VK_FORMAT_R8G8_UNORM:
      min_bytes_per_pixel = 2;
      break;

    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
      min_bytes_per_pixel = 4;
      break;

    default:
      ADD_FAILURE();
      break;
  }

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

  EXPECT_EQ(expected_count, properties.bufferCount);
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

bool VulkanExtensionTest::InitializeDirectImage(vk::BufferCollectionFUCHSIA collection,
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
  if (result != vk::Result::eSuccess) {
    ADD_FAILURE() << "vkCreateImage() failed: " << vk::to_string(result);
    return false;
  }
  vk_image_ = std::move(vk_image);
  return true;
}

std::optional<uint32_t> VulkanExtensionTest::InitializeDirectImageMemory(
    vk::BufferCollectionFUCHSIA collection, uint32_t expected_count) {
  const vk::Device &device = *ctx_->device();
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(device, *vk_image_, &requirements);
  uint32_t memory_type;
  ValidateBufferProperties(requirements, collection, expected_count, &memory_type);

  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIA,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(requirements.size)
                     .setMemoryTypeIndex(memory_type),
                 vk::ImportMemoryBufferCollectionFUCHSIA().setCollection(collection).setIndex(0),
                 vk::MemoryDedicatedAllocateInfoKHR().setImage(*vk_image_).setBuffer(*vk_buffer_));

  auto [result, vk_device_memory] =
      ctx_->device()->allocateMemoryUnique(alloc_info.get<vk::MemoryAllocateInfo>());
  if (result != vk::Result::eSuccess) {
    ADD_FAILURE() << "allocateMemoryUnique() failed: " << vk::to_string(result);
    return std::nullopt;
  }
  vk_device_memory_ = std::move(vk_device_memory);

  auto bind_result = ctx_->device()->bindImageMemory(*vk_image_, *vk_device_memory_, 0u);
  if (bind_result != vk::Result::eSuccess) {
    ADD_FAILURE() << "vkBindImageMemory() failed: " << vk::to_string(bind_result);
    return std::nullopt;
  }
  return memory_type;
}

VulkanExtensionTest::UniqueBufferCollection VulkanExtensionTest::CreateVkBufferCollectionForImage(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
    const vk::ImageFormatConstraintsInfoFUCHSIA constraints,
    vk::ImageConstraintsInfoFlagsFUCHSIA flags) {
  vk::BufferCollectionCreateInfoFUCHSIA import_info(token.Unbind().TakeChannel().release());
  auto [result, collection] =
      ctx_->device()->createBufferCollectionFUCHSIAUnique(import_info, nullptr, loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);

  vk::ImageConstraintsInfoFUCHSIA constraints_info;
  constraints_info.formatConstraintsCount = 1;
  constraints_info.pFormatConstraints = &constraints;
  constraints_info.bufferCollectionConstraints.minBufferCount = 1;
  constraints_info.bufferCollectionConstraints.minBufferCountForCamping = 0;
  constraints_info.bufferCollectionConstraints.minBufferCountForSharedSlack = 0;
  constraints_info.flags = flags;

  result = ctx_->device()->setBufferCollectionImageConstraintsFUCHSIA(*collection, constraints_info,
                                                                      loader_);
  EXPECT_EQ(result, vk::Result::eSuccess);
  return std::move(collection);
}

bool VulkanExtensionTest::Exec(
    VkFormat format, uint32_t width, uint32_t height, bool linear,
    bool repeat_constraints_as_non_protected,
    const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints) {
  EXPECT_NE(format, VK_FORMAT_UNDEFINED);

  auto [local_token, vulkan_token, non_protected_token] = MakeSharedCollection<3>();

  // This bool suggests that we dup another token to set the same constraints, skipping protected
  // memory requirements. This emulates another participant which does not require protected memory.
  UniqueBufferCollection non_protected_collection;
  bool is_yuv = (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) ||
                (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR);
  if (repeat_constraints_as_non_protected) {
    vk::ImageFormatConstraintsInfoFUCHSIA constraints =
        GetDefaultImageFormatConstraintsInfo(is_yuv);
    constraints.imageCreateInfo =
        GetDefaultImageCreateInfo(/*use_protected_memory=*/false, format, width, height, linear);
    non_protected_collection = CreateVkBufferCollectionForImage(
        std::move(non_protected_token), constraints,
        vk::ImageConstraintsInfoFlagBitsFUCHSIA::eProtectedOptional);
  } else {
    // Close the token to prevent sysmem from waiting on it.
    non_protected_token->Close();
    non_protected_token = {};
  }

  auto image_create_info =
      GetDefaultImageCreateInfo(use_protected_memory_, format, width, height, linear);
  vk::ImageFormatConstraintsInfoFUCHSIA constraints = GetDefaultImageFormatConstraintsInfo(is_yuv);
  constraints.imageCreateInfo = image_create_info;
  UniqueBufferCollection collection =
      CreateVkBufferCollectionForImage(std::move(vulkan_token), constraints);

  std::optional<fuchsia::sysmem::BufferCollectionConstraints> constraints_option;
  if (!format_constraints.empty()) {
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    // Use the other connection to specify the actual desired format and size,
    // which should be compatible with what the vulkan driver can use.
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    // Try multiple format modifiers.
    constraints.image_format_constraints_count = static_cast<uint32_t>(format_constraints.size());
    for (uint32_t i = 0; i < constraints.image_format_constraints_count; i++) {
      constraints.image_format_constraints[i] = format_constraints[i];
    }
    constraints_option = constraints;
  }
  auto buffer_collection_info =
      AllocateSysmemCollection(constraints_option, std::move(local_token));

  EXPECT_EQ(1u, buffer_collection_info.buffer_count);

  if (!InitializeDirectImage(*collection, image_create_info)) {
    ADD_FAILURE() << "InitializeDirectImage() failed";
    return false;
  }

  if (linear) {
    CheckLinearSubresourceLayout(format, width);
  }

  if (!InitializeDirectImageMemory(*collection)) {
    ADD_FAILURE() << "InitializeDirectImageMemory() failed";
    return false;
  }

  return true;
}

bool VulkanExtensionTest::ExecBuffer(uint32_t size) {
  VkResult result;
  const vk::Device &device = *ctx_->device();

  auto [local_token, vulkan_token] = MakeSharedCollection<2>();

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
  constraints.createInfo = buffer_create_info;
  constraints.requiredFormatFeatures = vk::FormatFeatureFlagBits::eVertexBuffer;
  constraints.bufferCollectionConstraints.minBufferCount = kMinBufferCount;

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

  {
    auto [result, vk_buffer] = ctx_->device()->createBufferUnique(buffer_create_info, nullptr);

    if (result != vk::Result::eSuccess) {
      RTN_MSG(false, "vkCreateBuffer failed: %d\n", result);
    }
    vk_buffer_ = std::move(vk_buffer);
  }

  vk::MemoryRequirements requirements;
  ctx_->device()->getBufferMemoryRequirements(*vk_buffer_, &requirements);
  uint32_t memory_type;
  ValidateBufferProperties(requirements, collection, kMinBufferCount, &memory_type);
  vk::BufferCollectionPropertiesFUCHSIA properties;
  EXPECT_EQ(vk::Result::eSuccess,
            ctx_->device()->getBufferCollectionPropertiesFUCHSIA(collection, &properties, loader_));

  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIA,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(requirements.size)
                     .setMemoryTypeIndex(memory_type),
                 vk::ImportMemoryBufferCollectionFUCHSIA().setCollection(collection).setIndex(1),
                 vk::MemoryDedicatedAllocateInfoKHR().setImage(*vk_image_).setBuffer(*vk_buffer_));

  auto [vk_result, vk_device_memory] =
      ctx_->device()->allocateMemoryUnique(alloc_info.get<vk::MemoryAllocateInfo>());
  EXPECT_EQ(vk_result, vk::Result::eSuccess);
  vk_device_memory_ = std::move(vk_device_memory);

  result = vkBindBufferMemory(device, *vk_buffer_, *vk_device_memory_, 0u);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  ctx_->device()->destroyBufferCollectionFUCHSIA(collection, nullptr, loader_);
  return true;
}

bool VulkanExtensionTest::IsMemoryTypeCoherent(uint32_t memoryTypeIndex) {
  vk::PhysicalDeviceMemoryProperties props = ctx_->physical_device().getMemoryProperties();
  assert(memoryTypeIndex < props.memoryTypeCount);
  return static_cast<bool>(props.memoryTypes[memoryTypeIndex].propertyFlags &
                           vk::MemoryPropertyFlagBits::eHostCoherent);
}

void VulkanExtensionTest::WriteLinearImage(vk::DeviceMemory memory, bool is_coherent,
                                           uint32_t width, uint32_t height, uint32_t fill) {
  void *addr;
  vk::Result result =
      ctx_->device()->mapMemory(memory, 0 /* offset */, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &addr);
  ASSERT_EQ(vk::Result::eSuccess, result);

  for (uint32_t i = 0; i < width * height; i++) {
    reinterpret_cast<uint32_t *>(addr)[i] = fill;
  }

  if (!is_coherent) {
    auto range = vk::MappedMemoryRange().setMemory(memory).setSize(VK_WHOLE_SIZE);
    EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->flushMappedMemoryRanges(1, &range));
  }

  ctx_->device()->unmapMemory(memory);
}

void VulkanExtensionTest::CheckLinearImage(vk::DeviceMemory memory, bool is_coherent,
                                           uint32_t width, uint32_t height, uint32_t fill) {
  void *addr;
  vk::Result result =
      ctx_->device()->mapMemory(memory, 0 /* offset */, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &addr);
  ASSERT_EQ(vk::Result::eSuccess, result);

  if (!is_coherent) {
    auto range = vk::MappedMemoryRange().setMemory(memory).setSize(VK_WHOLE_SIZE);
    EXPECT_EQ(vk::Result::eSuccess, ctx_->device()->invalidateMappedMemoryRanges(1, &range));
  }

  uint32_t error_count = 0;
  constexpr uint32_t kMaxErrors = 10;
  for (uint32_t i = 0; i < width * height; i++) {
    EXPECT_EQ(fill, reinterpret_cast<uint32_t *>(addr)[i]) << "i " << i;
    if (reinterpret_cast<uint32_t *>(addr)[i] != fill) {
      error_count++;
      if (error_count > kMaxErrors) {
        printf("Skipping reporting remaining errors\n");
        break;
      }
    }
  }

  ctx_->device()->unmapMemory(memory);
}

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

TEST_F(VulkanExtensionTest, BufferCollectionProtectedBuffer) {
  set_use_protected_memory(true);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(device_supports_protected_memory());
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

  // The image may be linear or y-tiled, but since all pixels are the same and
  // the dimensions are a multiple of the tile size then pretending it's linear
  // should be ok.
  CheckLinearImage(memory.get(), src_is_coherent, kDefaultWidth, kDefaultHeight, 0xffffffff);
}

// Test on UNORM and SRGB, because on older Intel devices UNORM supports CCS_E, but SRGB only
// supports CCS_D.
INSTANTIATE_TEST_SUITE_P(, VulkanFormatTest,
                         ::testing::Values(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB),
                         [](const testing::TestParamInfo<VulkanFormatTest::ParamType> &info) {
                           return vk::to_string(static_cast<vk::Format>(info.param));
                         });

}  // namespace
