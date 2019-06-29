// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl_test_base.h>
#include <gtest/gtest.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>
#include <vulkan/vulkan.h>

#include <chrono>
#include <mutex>
#include <set>

class TestSwapchain {
 public:
  template <class T>
  void LoadProc(T* proc, const char* name) {
    auto get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vkGetInstanceProcAddr(vk_instance_, "vkGetDeviceProcAddr"));
    *proc = reinterpret_cast<T>(get_device_proc_addr(vk_device_, name));
  }

  TestSwapchain(bool protected_memory) : protected_memory_(protected_memory) {
    std::vector<const char*> instance_layers{"VK_LAYER_FUCHSIA_imagepipe_swapchain"};
    std::vector<const char*> instance_ext{VK_KHR_SURFACE_EXTENSION_NAME,
                                          VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME};
    std::vector<const char*> device_ext{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "test",
        .applicationVersion = 0,
        .pEngineName = "test",
        .engineVersion = 0,
        .apiVersion = VK_MAKE_VERSION(1, 1, 0),
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
        .ppEnabledLayerNames = instance_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instance_ext.size()),
        .ppEnabledExtensionNames = instance_ext.data(),
    };

    VkResult result;
    result = vkCreateInstance(&inst_info, nullptr, &vk_instance_);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateInstance failed: %d\n", result);
      return;
    }

    uint32_t gpu_count = 1;
    std::vector<VkPhysicalDevice> physical_devices(gpu_count);
    result = vkEnumeratePhysicalDevices(vk_instance_, &gpu_count, physical_devices.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
      fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", result);
      return;
    }

    VkPhysicalDeviceProtectedMemoryFeatures protected_memory_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
        .pNext = nullptr,
        .protectedMemory = VK_FALSE,
    };
    if (protected_memory_) {
      PFN_vkGetPhysicalDeviceFeatures2 get_physical_device_features_2_ =
          reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
              vkGetInstanceProcAddr(vk_instance_, "vkGetPhysicalDeviceFeatures2"));
      if (!get_physical_device_features_2_) {
        fprintf(stderr, "Failed to find vkGetPhysicalDeviceFeatures2\n");
        return;
      }
      VkPhysicalDeviceFeatures2 physical_device_features = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
          .pNext = &protected_memory_features};
      get_physical_device_features_2_(physical_devices[0], &physical_device_features);
      protected_memory_is_supported_ = protected_memory_features.protectedMemory;
      if (!protected_memory_is_supported_) {
        fprintf(stderr, "Protected memory is not supported\n");
        return;
      }
    }

    float queue_priorities[1] = {0.0};
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
        .flags = 0};

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = protected_memory_ ? &protected_memory_features : nullptr,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(device_ext.size()),
        .ppEnabledExtensionNames = device_ext.data(),
        .pEnabledFeatures = nullptr};

    result = vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &vk_device_);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateDevice failed: %d\n", result);
      return;
    }

    PFN_vkGetDeviceProcAddr get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
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

  VkResult CreateSwapchainHelper(VkSurfaceKHR surface, VkSwapchainKHR* swapchain_out) {
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = protected_memory_ ? VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR
                                   : static_cast<VkSwapchainCreateFlagsKHR>(0),
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

    return create_swapchain_khr_(vk_device_, &create_info, nullptr, swapchain_out);
  }

  void Surface() {
    ASSERT_TRUE(init_);

    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS,
              vkCreateImagePipeSurfaceFUCHSIA(vk_instance_, &create_info, nullptr, &surface));
    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  void CreateSwapchain() {
    ASSERT_TRUE(init_);

    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS,
              vkCreateImagePipeSurfaceFUCHSIA(vk_instance_, &create_info, nullptr, &surface));

    VkSwapchainKHR swapchain;
    EXPECT_EQ(VK_SUCCESS, CreateSwapchainHelper(surface, &swapchain));

    destroy_swapchain_khr_(vk_device_, swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  VkInstance vk_instance_;
  VkDevice vk_device_;
  PFN_vkCreateSwapchainKHR create_swapchain_khr_;
  PFN_vkDestroySwapchainKHR destroy_swapchain_khr_;
  PFN_vkGetSwapchainImagesKHR get_swapchain_images_khr_;
  PFN_vkAcquireNextImageKHR acquire_next_image_khr_;
  PFN_vkQueuePresentKHR queue_present_khr_;

  const bool protected_memory_ = false;
  bool init_ = false;
  bool protected_memory_is_supported_ = false;
};

class SwapchainTest : public ::testing::TestWithParam<bool /* protected_memory */> {};

TEST_P(SwapchainTest, Surface) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_)
    return;
  ASSERT_TRUE(test.init_);

  test.Surface();
}

TEST_P(SwapchainTest, Create) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_)
    return;
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain();
}

INSTANTIATE_TEST_SUITE_P(SwapchainTestSuite, SwapchainTest, ::testing::Bool());

namespace {

uint64_t ZirconIdFromHandle(uint32_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK)
    return 0;
  return info.koid;
}

// FakeImagePipe runs async loop on its own thread to allow the test
// to use blocking Vulkan calls while present callbacks are processed.
class FakeImagePipe : public fuchsia::images::testing::ImagePipe_TestBase {
 public:
  FakeImagePipe(fidl::InterfaceRequest<fuchsia::images::ImagePipe> request)
      : loop_(&kAsyncLoopConfigNoAttachToThread),
        binding_(this, std::move(request), loop_.dispatcher()) {
    loop_.StartThread();
  }

  ~FakeImagePipe() { loop_.Shutdown(); }

  void NotImplemented_(const std::string& name) override {}

  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info, ::zx::vmo memory,
                uint64_t offset_bytes, uint64_t size_bytes,
                fuchsia::images::MemoryType memory_type) override {
    // Do nothing.
  }

  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    ::std::vector<::zx::event> acquire_fences,
                    ::std::vector<::zx::event> release_fences,
                    PresentImageCallback callback) override {
    std::unique_lock<std::mutex> lock(mutex_);

    acquire_fences_.insert(ZirconIdFromHandle(acquire_fences[0].get()));

    zx_signals_t pending;
    zx_status_t status =
        acquire_fences[0].wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::sec(10)), &pending);

    if (status == ZX_OK) {
      release_fences[0].signal(0, ZX_EVENT_SIGNALED);
      callback({0, 0});
    }
    presented_.push_back({image_id, status});
  }

  uint32_t presented_count() {
    std::unique_lock<std::mutex> lock(mutex_);
    return presented_.size();
  }

  uint32_t acquire_fences_count() {
    std::unique_lock<std::mutex> lock(mutex_);
    return acquire_fences_.size();
  }

  struct Presented {
    uint32_t image_id;
    zx_status_t acquire_wait_status;
  };

 private:
  async::Loop loop_;
  fidl::Binding<fuchsia::images::ImagePipe> binding_;
  std::mutex mutex_;
  std::vector<Presented> presented_;
  std::set<uint64_t> acquire_fences_;
};

}  // namespace

class SwapchainFidlTest : public ::testing::TestWithParam<bool /* protected_memory */> {};

TEST_P(SwapchainFidlTest, PresentAndAcquireNoSemaphore) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_)
    return;
  ASSERT_TRUE(test.init_);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  std::unique_ptr<FakeImagePipe> imagepipe_ = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe>(std::move(remote_endpoint)));

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS, test.CreateSwapchainHelper(surface, &swapchain));

  VkQueue queue;
  vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);

  uint32_t image_index;
  // Acquire all initial images.
  for (uint32_t i = 0; i < 3; i++) {
    EXPECT_EQ(VK_SUCCESS,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(i, image_index);
  }

  EXPECT_EQ(VK_NOT_READY,
            test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                         VK_NULL_HANDLE, &image_index));

  uint32_t present_index;  // Initialized below.
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

  constexpr uint32_t kFrameCount = 100;
  for (uint32_t i = 0; i < kFrameCount; i++) {
    present_index = i % 3;
    ASSERT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));

    constexpr uint64_t kTimeoutNs = std::chrono::nanoseconds(std::chrono::seconds(10)).count();
    ASSERT_EQ(VK_SUCCESS,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, kTimeoutNs, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(present_index, image_index);

    EXPECT_EQ(VK_NOT_READY,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
  }

  test.destroy_swapchain_khr_(test.vk_device_, swapchain, nullptr);
  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);

  EXPECT_EQ(kFrameCount, imagepipe_->presented_count());
  EXPECT_EQ(kFrameCount, imagepipe_->acquire_fences_count());
}

INSTANTIATE_TEST_SUITE_P(SwapchainFidlTestSuite, SwapchainFidlTest, ::testing::Bool());
