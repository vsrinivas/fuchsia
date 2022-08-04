// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

namespace {

vk::BindSparseInfo bind_sparse_info;

// This function will generate a |vk::ImageCreateInfo| which will cause no errors / warnings in
// Vulkan validation layers.
auto CorrectImageCreateInfo() {
  vk::ImageCreateInfo create_info;
  create_info.pNext = nullptr;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = vk::Format::eR8G8B8A8Unorm;
  create_info.extent = vk::Extent3D{128, 128, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  create_info.flags = vk::ImageCreateFlags();
  return create_info;
}

// This function will generate a |vk::ImageCreateInfo| which will cause the following validation
// error when creating |vk::Image| using this create info:
// Validation Error: [ VUID-VkImageCreateInfo-pNext-pNext ] Object 0: handle = 0xa605cd0a8, type =
// VK_OBJECT_TYPE_DEVICE; | MessageID = 0x69dad144 | vkCreateImage: pCreateInfo->pNext chain
// includes a structure with unexpected VkStructureType VK_STRUCTURE_TYPE_BIND_SPARSE_INFO; [...]
// This error is based on the Valid Usage documentation for version 198 of the Vulkan header.  It is
// possible that you are using a struct from a private extension or an extension that was added to a
// later version of the Vulkan header, in which case the use of pCreateInfo->pNext is undefined and
// may not work correctly with validation enabled The Vulkan spec states: Each pNext member of any
// structure (including this one) in the pNext chain must be either NULL or a pointer to a valid
// instance of [...]
// (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-VkImageCreateInfo-pNext-pNext)
auto ErrorImageCreateInfo() {
  vk::ImageCreateInfo create_info = CorrectImageCreateInfo();
  // VK_STRUCTURE_TYPE_BIND_SPARSE_INFO is not legal to chain onto
  // vk::ImageCreateInfo and almost certainly will never be. It's unlikely to
  // cause asserts in drivers, since they're likely to ignore invalid structs.
  create_info.pNext = &bind_sparse_info;
  return create_info;
}

}  // anonymous namespace

VK_TEST(ValidationLayer, ValidationLayerIsSupported) {
  std::optional<std::string> validation_layer_name = VulkanInstance::GetValidationLayerName();

  ASSERT_TRUE(validation_layer_name);
  ASSERT_TRUE(*validation_layer_name == "VK_LAYER_KHRONOS_validation");

  VulkanInstance::Params instance_params{
      {*validation_layer_name},              // layer_names
      {VK_EXT_DEBUG_REPORT_EXTENSION_NAME},  // extension_names
      false                                  // requires_surface
  };
  VulkanInstancePtr vulkan_instance = VulkanInstance::New(instance_params);
  ASSERT_TRUE(vulkan_instance);
}

using ValidationLayerDefaultHandler = TestWithVkValidationLayer;

// This test tests the |TestWithVkValidationLayer| class.
VK_TEST_F(ValidationLayerDefaultHandler, HandlerTest) {
  Escher* escher = GetEscher();

  auto device = escher->vk_device();
  {
    auto create_info = ErrorImageCreateInfo();
    auto create_result = device.createImage(create_info);
    auto vk_image = create_result.value;
    device.destroyImage(vk_image);
    EXPECT_VK_SUCCESS(device.waitIdle());
  }
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);

  // In this case the |createImage()| command is valid. We should not see any new Vulkan validation
  // errors nor warnings here.
  {
    auto create_info = CorrectImageCreateInfo();
    auto create_result = device.createImage(create_info);
    auto vk_image = create_result.value;
    device.destroyImage(vk_image);
    EXPECT_VK_SUCCESS(device.waitIdle());
  }
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);

  // Suppress the debug reports check in |TearDown()|.
  SUPPRESS_VK_VALIDATION_DEBUG_REPORTS();
}

class ValidationLayerWithCustomHandler : public TestWithVkValidationLayer {
 public:
  ValidationLayerWithCustomHandler()
      : TestWithVkValidationLayer(
            {{[this](VkDebugReportFlagsEXT flags_in, VkDebugReportObjectTypeEXT object_type_in,
                     uint64_t object, size_t location, int32_t message_code,
                     const char* pLayerPrefix, const char* pMessage, void* pUserData) -> bool {
                if (VK_DEBUG_REPORT_ERROR_BIT_EXT & flags_in) {
                  ++count_errors_;
                }
                return false;
              },
              nullptr}}) {}
  int GetCountErrors() const { return count_errors_; }

 private:
  int count_errors_ = 0;
};

VK_TEST_F(ValidationLayerWithCustomHandler, HandlerTest) {
  Escher* escher = GetEscher();
  auto device = escher->vk_device();
  {
    auto create_info = ErrorImageCreateInfo();
    auto create_result = device.createImage(create_info);
    auto vk_image = create_result.value;
    device.destroyImage(vk_image);
    EXPECT_VK_SUCCESS(device.waitIdle());
  }
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);
  EXPECT_EQ(GetCountErrors(), 1);

  // In this case the createImage() command is valid. We should not see any Vulkan validation
  // errors nor warnings here.
  {
    auto create_info = CorrectImageCreateInfo();
    auto create_result = device.createImage(create_info);
    auto vk_image = create_result.value;
    device.destroyImage(vk_image);
    EXPECT_VK_SUCCESS(device.waitIdle());
  }
  // no new errors occurred.
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);
  EXPECT_EQ(GetCountErrors(), 1);

  // Suppress the debug reports check in |TearDown()|.
  SUPPRESS_VK_VALIDATION_DEBUG_REPORTS();
}

}  // namespace test
}  // namespace escher
