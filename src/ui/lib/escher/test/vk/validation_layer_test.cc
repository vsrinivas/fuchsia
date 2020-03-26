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

// This function will generate a |vk::ImageCreateInfo| which will cause the following validation
// error when creating |vk::Image| using this create info:
//     VUID-VkImageCreateInfo-usage-requiredbitmask(ERROR / SPEC): msgNum: 0 - vkCreateImage:
//     value of pCreateInfo->usage must not be 0. The Vulkan spec states: usage must not be 0
//     (https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html
//      #VUID-VkImageCreateInfo-usage-requiredbitmask)
auto ErrorImageCreateInfo() {
  vk::ImageCreateInfo create_info;
  create_info.pNext = nullptr;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = vk::Format::eR8G8B8A8Unorm;
  create_info.extent = vk::Extent3D{128, 128, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = vk::ImageUsageFlags();
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  create_info.flags = vk::ImageCreateFlags();
  return create_info;
}

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

}  // anonymous namespace

VK_TEST(ValidationLayer, ValidationLayerIsSupported) {
  std::optional<std::string> validation_layer_name = VulkanInstance::GetValidationLayerName();

  ASSERT_TRUE(validation_layer_name);
  ASSERT_TRUE(*validation_layer_name == "VK_LAYER_KHRONOS_validation" ||
              *validation_layer_name == "VK_LAYER_LUNARG_standard_validation");

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
    device.waitIdle();
  }
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);

  // In this case the |createImage()| command is valid. We should not see any new Vulkan validation
  // errors nor warnings here.
  {
    auto create_info = CorrectImageCreateInfo();
    auto create_result = device.createImage(create_info);
    auto vk_image = create_result.value;
    device.destroyImage(vk_image);
    device.waitIdle();
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
    device.waitIdle();
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
    device.waitIdle();
  }
  // no new errors occurred.
  EXPECT_VULKAN_VALIDATION_ERRORS_EQ(1);
  EXPECT_EQ(GetCountErrors(), 1);

  // Suppress the debug reports check in |TearDown()|.
  SUPPRESS_VK_VALIDATION_DEBUG_REPORTS();
}

}  // namespace test
}  // namespace escher
