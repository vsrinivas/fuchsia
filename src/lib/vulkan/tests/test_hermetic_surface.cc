// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include <cstdio>
#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

namespace {

// These tests are hemetic because they're run against the fake hardware display controller provider
// and don't need to connect to the real display controller.  They still need sysmem and a Vulkan
// implementation.
class TestSurface {
 public:
  TestSurface() {
    const char* layer_name = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
    std::vector<const char*> instance_layers{layer_name};
    std::vector<const char*> instance_ext{VK_KHR_SURFACE_EXTENSION_NAME,
                                          VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME};

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .pApplicationInfo = nullptr,
        .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
        .ppEnabledLayerNames = instance_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instance_ext.size()),
        .ppEnabledExtensionNames = instance_ext.data(),
    };

    VkResult ret = vkCreateInstance(&inst_info, nullptr, &vk_instance_);
    if (ret != VK_SUCCESS) {
      fprintf(stderr, "ERROR: vkCreateInstance() returned: %d\n", ret);
      return;
    }

    init_ = true;
  }

  ~TestSurface() {
    if (vk_instance_) {
      vkDestroyInstance(vk_instance_, nullptr);
    }
  }

  void CreateSurface(bool use_dynamic_symbol) {
    ASSERT_TRUE(init_);

    PFN_vkCreateImagePipeSurfaceFUCHSIA f_vkCreateImagePipeSurfaceFUCHSIA =
        use_dynamic_symbol
            ? reinterpret_cast<PFN_vkCreateImagePipeSurfaceFUCHSIA>(
                  vkGetInstanceProcAddr(vk_instance_, "vkCreateImagePipeSurfaceFUCHSIA"))
            : vkCreateImagePipeSurfaceFUCHSIA;
    ASSERT_TRUE(f_vkCreateImagePipeSurfaceFUCHSIA);

    zx::channel endpoint0, endpoint1;

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .imagePipeHandle = ZX_HANDLE_INVALID,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result =
        f_vkCreateImagePipeSurfaceFUCHSIA(vk_instance_, &create_info, nullptr, &surface);
    EXPECT_EQ(VK_SUCCESS, result);
    if (VK_SUCCESS == result) {
      vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
    }
  }

  VkInstance vk_instance_ = VK_NULL_HANDLE;
  bool init_ = false;
};

TEST(HermeticSurface, CreateFramebufferSurface) { TestSurface().CreateSurface(false); }

TEST(HermeticSurface, CreateFramebufferSurfaceDynamicSymbol) { TestSurface().CreateSurface(true); }

}  // namespace
