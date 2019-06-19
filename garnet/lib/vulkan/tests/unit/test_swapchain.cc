// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl_test_base.h>
#include <gtest/gtest.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/zx/channel.h>
#include <vulkan/vulkan.h>

#include <set>

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
        "VK_LAYER_FUCHSIA_imagepipe_swapchain"};
    std::vector<const char*> instance_ext{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME};
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

    VkResult result;
    result = vkCreateInstance(&inst_info, nullptr, &vk_instance_);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateInstance failed: %d\n", result);
      return;
    }

    uint32_t gpu_count = 1;
    std::vector<VkPhysicalDevice> physical_devices(gpu_count);
    result = vkEnumeratePhysicalDevices(vk_instance_, &gpu_count,
                                        physical_devices.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
      fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", result);
      return;
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
        .pNext = nullptr,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(device_ext.size()),
        .ppEnabledExtensionNames = device_ext.data(),
        .pEnabledFeatures = nullptr};

    result = vkCreateDevice(physical_devices[0], &device_create_info, nullptr,
                            &vk_device_);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateDevice failed: %d\n", result);
      return;
    }

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
    ASSERT_TRUE(init_);

    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS, vkCreateImagePipeSurfaceFUCHSIA(
                              vk_instance_, &create_info, nullptr, &surface));
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
    EXPECT_EQ(VK_SUCCESS, vkCreateImagePipeSurfaceFUCHSIA(
                              vk_instance_, &create_info, nullptr, &surface));

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

  bool init_ = false;
};

TEST(Swapchain, Surface) { TestSwapchain().Surface(); }

TEST(Swapchain, Create) { TestSwapchain().CreateSwapchain(); }

namespace {

uint64_t ZirconIdFromHandle(uint32_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  if (status != ZX_OK)
    return 0;
  return info.koid;
}

class FakeImagePipe : public fuchsia::images::testing::ImagePipe_TestBase {
 public:
  FakeImagePipe(fidl::InterfaceRequest<fuchsia::images::ImagePipe> request)
      : binding_(this, std::move(request)) {}

  void NotImplemented_(const std::string& name) override {
    printf("%s\n", name.c_str());
  }

  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                ::zx::vmo memory, uint64_t offset_bytes, uint64_t size_bytes,
                fuchsia::images::MemoryType memory_type) override {
    // Do nothing.
  }

  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    ::std::vector<::zx::event> acquire_fences,
                    ::std::vector<::zx::event> release_fences,
                    PresentImageCallback callback) override {
    acquire_fences_.insert(ZirconIdFromHandle(acquire_fences[0].get()));

    zx_signals_t pending;
    zx_status_t status = acquire_fences[0].wait_one(
        ZX_EVENT_SIGNALED, zx::deadline_after(zx::sec(10)), &pending);

    if (status == ZX_OK) {
      release_fences[0].signal(0, ZX_EVENT_SIGNALED);
      callback({0, 0});
    }
    presented_.push_back({image_id, status});
  }

  struct Presented {
    uint32_t image_id;
    zx_status_t acquire_wait_status;
  };

  std::vector<Presented> presented_;
  std::set<uint64_t> acquire_fences_;

 private:
  fidl::Binding<fuchsia::images::ImagePipe> binding_;
};

}  // namespace

class SwapchainFidlTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override { TestLoopFixture::SetUp(); }

  std::unique_ptr<FakeImagePipe> imagepipe_;
};

TEST_F(SwapchainFidlTest, PresentAndAcquireNoSemaphore) {
  TestSwapchain test;
  ASSERT_TRUE(test.init_);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  imagepipe_ = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe>(
          std::move(remote_endpoint)));

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info,
                                            nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS, test.CreateSwapchainHelper(surface, &swapchain));

  VkQueue queue;
  vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);

  uint32_t image_index;
  // Acquire all initial images.
  for (uint32_t i = 0; i < 3; i++) {
    EXPECT_EQ(VK_SUCCESS, test.acquire_next_image_khr_(
                              test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                              VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(i, image_index);
  }

  EXPECT_EQ(VK_NOT_READY, test.acquire_next_image_khr_(
                              test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
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

    // Run loop generates calls into FakeImagePipe::PresentImage
    RunLoopUntilIdle();

    constexpr uint32_t kTimeout = 10000000ull;
    ASSERT_EQ(VK_SUCCESS, test.acquire_next_image_khr_(
                              test.vk_device_, swapchain, kTimeout,
                              VK_NULL_HANDLE, VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(present_index, image_index);

    EXPECT_EQ(VK_NOT_READY, test.acquire_next_image_khr_(
                                test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                VK_NULL_HANDLE, &image_index));
  }

  test.destroy_swapchain_khr_(test.vk_device_, swapchain, nullptr);
  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);

  EXPECT_EQ(kFrameCount, imagepipe_->presented_.size());
  EXPECT_EQ(kFrameCount, imagepipe_->acquire_fences_.size());
}
