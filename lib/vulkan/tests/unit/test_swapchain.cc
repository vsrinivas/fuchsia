// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/vulkan/src/swapchain/image_pipe_surface.h"
#include "gtest/gtest.h"

class MockImagePipeSurface : public image_pipe_swapchain::ImagePipeSurface {
 public:
  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                zx::vmo buffer, uint64_t size_bytes) override {}
  void RemoveImage(uint32_t image_id) override {}
  void PresentImage(uint32_t image_id,
                    std::vector<zx::event> acquire_fences,
                    std::vector<zx::event> release_fences) override {
    presented_.push_back(
        {image_id, std::move(acquire_fences), std::move(release_fences)});
  }

  struct Presented {
    uint32_t image_id;
    std::vector<zx::event> acquire_fences;
    std::vector<zx::event> release_fences;
  };

  std::vector<Presented> presented_;
};

class TestSwapchain {
 public:
  template <class T>
  void LoadProc(T* proc, const char* name) {
    auto get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vkGetInstanceProcAddr(vk_instance_, "vkGetDeviceProcAddr"));
    *proc = reinterpret_cast<T>(get_device_proc_addr(vk_device_, name));
  }

  TestSwapchain() {
    std::vector<const char*> instance_layers{
        "VK_LAYER_GOOGLE_image_pipe_swapchain"};
    std::vector<const char*> instance_ext{VK_KHR_SURFACE_EXTENSION_NAME,
                                          VK_KHR_MAGMA_SURFACE_EXTENSION_NAME};
    std::vector<const char*> device_ext{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .pApplicationInfo = nullptr,
        .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
        .ppEnabledLayerNames = instance_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instance_ext.size()),
        .ppEnabledExtensionNames = instance_ext.data(),
    };

    if (VK_SUCCESS != vkCreateInstance(&inst_info, nullptr, &vk_instance_))
      return;

    uint32_t gpu_count = 1;
    std::vector<VkPhysicalDevice> physical_devices(gpu_count);
    if (VK_SUCCESS != vkEnumeratePhysicalDevices(vk_instance_, &gpu_count,
                                                 physical_devices.data()))
      return;

    float queue_priorities[1] = {0.0};
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
        .flags = 0};

    std::vector<const char*> enabled_layers{
        "VK_LAYER_GOOGLE_image_pipe_swapchain"};

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(device_ext.size()),
        .ppEnabledExtensionNames = device_ext.data(),
        .pEnabledFeatures = nullptr};

    if (VK_SUCCESS != vkCreateDevice(physical_devices[0], &device_create_info,
                                     nullptr, &vk_device_))
      return;

    PFN_vkGetDeviceProcAddr get_device_proc_addr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vkGetInstanceProcAddr(vk_instance_, "vkGetDeviceProcAddr"));
    if (!get_device_proc_addr) {
      fprintf(stderr, "Failed to find vkGetDeviceProcAddr\n");
      return;
    }

    LoadProc(&create_swapchain_khr_, "vkCreateSwapchainKHR");
    LoadProc(&destroy_swapchain_khr_, "vkDestroySwapchainKHR");
    LoadProc(&get_swapchain_images_khr_, "vkGetSwapchainImagesKHR");
    LoadProc(&acquire_next_image_khr_, "vkAcquireNextImageKHR");
    LoadProc(&queue_present_khr_, "vkQueuePresentKHR");

    init_ = true;
  }

  VkResult CreateSwapchainHelper(VkSurfaceKHR surface,
                                 VkSwapchainKHR* swapchain_out) {
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = 3,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageArrayLayers = 1,
        .imageExtent = {100, 100},
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    return create_swapchain_khr_(vk_device_, &create_info, nullptr,
                                 swapchain_out);
  }

  void Surface() {
    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkMagmaSurfaceCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS, vkCreateMagmaSurfaceKHR(vk_instance_, &create_info,
                                                  nullptr, &surface));
    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  void CreateSwapchain() {
    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkMagmaSurfaceCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS, vkCreateMagmaSurfaceKHR(vk_instance_, &create_info,
                                                  nullptr, &surface));

    VkSwapchainKHR swapchain;
    EXPECT_EQ(VK_SUCCESS, CreateSwapchainHelper(surface, &swapchain));

    destroy_swapchain_khr_(vk_device_, swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  void AcquireNoSemaphore() {
    ASSERT_TRUE(init_);

    MockImagePipeSurface surface;
    VkSwapchainKHR swapchain;

    ASSERT_EQ(VK_SUCCESS,
              CreateSwapchainHelper(reinterpret_cast<VkSurfaceKHR>(&surface),
                                    &swapchain));

    uint32_t image_index;
    EXPECT_EQ(VK_SUCCESS,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(0u, image_index);
    EXPECT_EQ(VK_SUCCESS,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(1u, image_index);
    EXPECT_EQ(VK_SUCCESS,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(2u, image_index);
    EXPECT_EQ(VK_NOT_READY,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));

    VkQueue queue;
    vkGetDeviceQueue(vk_device_, 0, 0, &queue);

    uint32_t present_index = 0;
    VkResult present_result;
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &present_index,
        .pResults = &present_result,
    };
    EXPECT_EQ(VK_SUCCESS, queue_present_khr_(queue, &present_info));
    EXPECT_EQ(1u, surface.presented_.size());
    EXPECT_EQ(1u, surface.presented_[0].acquire_fences.size());
    ASSERT_EQ(1u, surface.presented_[0].release_fences.size());
    surface.presented_[0].release_fences[0].signal(0, ZX_EVENT_SIGNALED);
    surface.presented_.erase(surface.presented_.begin());

    EXPECT_EQ(VK_SUCCESS,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(0u, image_index);
    EXPECT_EQ(VK_NOT_READY,
              acquire_next_image_khr_(vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, &image_index));
  }

  VkInstance vk_instance_;
  VkDevice vk_device_;
  PFN_vkCreateSwapchainKHR create_swapchain_khr_;
  PFN_vkDestroySwapchainKHR destroy_swapchain_khr_;
  PFN_vkGetSwapchainImagesKHR get_swapchain_images_khr_;
  PFN_vkAcquireNextImageKHR acquire_next_image_khr_;
  PFN_vkQueuePresentKHR queue_present_khr_;

  bool init_ = false;
};

TEST(Swapchain, AcquireNoSemaphore) { TestSwapchain().AcquireNoSemaphore(); }

TEST(Swapchain, Surface) { TestSwapchain().Surface(); }

TEST(Swapchain, Create) { TestSwapchain().CreateSwapchain(); }
