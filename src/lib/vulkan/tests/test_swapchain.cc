// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>

#include <chrono>
#include <future>
#include <mutex>
#include <set>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

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
class FakeImagePipe : public fuchsia::images::testing::ImagePipe2_TestBase {
 public:
  FakeImagePipe(fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request, bool should_present)
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        binding_(this, std::move(request), loop_.dispatcher()),
        should_present_(should_present) {
    loop_.StartThread();
  }

  ~FakeImagePipe() { loop_.Shutdown(); }

  void NotImplemented_(const std::string& name) override {}

  void AddBufferCollection(uint32_t buffer_collection_id,
                           ::fidl::InterfaceHandle<::fuchsia::sysmem::BufferCollectionToken>
                               buffer_collection_token) override {
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    status = sysmem_allocator->BindSharedCollection(buffer_collection_token.BindSync(),
                                                    buffer_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);

    fuchsia::sysmem::BufferCollectionConstraints constraints;
    status = buffer_collection->SetConstraints(false, constraints);
    EXPECT_EQ(status, ZX_OK);

    status = buffer_collection->Close();
    EXPECT_EQ(status, ZX_OK);
  }

  void AddImage(uint32_t image_id, uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                ::fuchsia::sysmem::ImageFormat_2 image_format) override {
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
      if (should_present_) {
        release_fences[0].signal(0, ZX_EVENT_SIGNALED);
        callback({0, 0});
      }
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
  fidl::Binding<fuchsia::images::ImagePipe2> binding_;
  bool should_present_;
  std::mutex mutex_;
  std::vector<Presented> presented_;
  std::set<uint64_t> acquire_fences_;
};

}  // namespace
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_utils_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                     VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                     const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
  fprintf(stderr, "Got debug utils callback: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}
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
                                          VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
                                          VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    std::vector<const char*> device_ext{VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                        VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME};

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
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(physical_devices[0], &properties);
      if (properties.apiVersion < VK_MAKE_VERSION(1, 1, 0)) {
        protected_memory_is_supported_ = false;
        fprintf(stderr, "Vulkan 1.1 is not supported by device\n");
        return;
      }

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

    auto pfnCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(vk_instance_, "vkCreateDebugUtilsMessengerEXT"));

    VkDebugUtilsMessengerCreateInfoEXT callback = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
        .pfnUserCallback = debug_utils_callback,
        .pUserData = nullptr};
    result = pfnCreateDebugUtilsMessengerEXT(vk_instance_, &callback, NULL, &messenger_cb_);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Failed to install debug callback\n");
      return;
    }
    float queue_priorities[1] = {0.0};
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
        .flags = protected_memory_
                     ? static_cast<VkDeviceQueueCreateFlags>(VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)
                     : 0};

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
    LoadProc(&get_device_queue2_, "vkGetDeviceQueue2");

    init_ = true;
  }

  ~TestSwapchain() {
    if (messenger_cb_) {
      auto pfnDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(vk_instance_, "vkDestroyDebugUtilsMessengerEXT"));
      pfnDestroyDebugUtilsMessengerEXT(vk_instance_, messenger_cb_, nullptr);
    }
  }

  VkResult CreateSwapchainHelper(VkSurfaceKHR surface, VkFormat format, VkImageUsageFlags usage,
                                 VkSwapchainKHR* swapchain_out) {
    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = protected_memory_ ? VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR
                                   : static_cast<VkSwapchainCreateFlagsKHR>(0),
        .surface = surface,
        .minImageCount = 3,
        .imageFormat = format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageArrayLayers = 1,
        .imageExtent = {100, 100},
        .imageUsage = usage,
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

  void Surface(bool use_dynamic_symbol) {
    ASSERT_TRUE(init_);

    PFN_vkCreateImagePipeSurfaceFUCHSIA f_vkCreateImagePipeSurfaceFUCHSIA =
        use_dynamic_symbol
            ? reinterpret_cast<PFN_vkCreateImagePipeSurfaceFUCHSIA>(
                  vkGetInstanceProcAddr(vk_instance_, "vkCreateImagePipeSurfaceFUCHSIA"))
            : vkCreateImagePipeSurfaceFUCHSIA;
    ASSERT_TRUE(f_vkCreateImagePipeSurfaceFUCHSIA);

    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS,
              f_vkCreateImagePipeSurfaceFUCHSIA(vk_instance_, &create_info, nullptr, &surface));
    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  void CreateSwapchain(int num_swapchains, VkFormat format, VkImageUsageFlags usage) {
    ASSERT_TRUE(init_);

    zx::channel endpoint0, endpoint1;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

    // Create FakeImagePipe that can consume BuffercollectionToken.
    imagepipe_ = std::make_unique<FakeImagePipe>(
        fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(endpoint1)),
        /*should_present=*/true);

    VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
        .imagePipeHandle = endpoint0.release(),
        .pNext = nullptr,
    };
    VkSurfaceKHR surface;
    EXPECT_EQ(VK_SUCCESS,
              vkCreateImagePipeSurfaceFUCHSIA(vk_instance_, &create_info, nullptr, &surface));

    for (int i = 0; i < num_swapchains; ++i) {
      VkSwapchainKHR swapchain;
      EXPECT_EQ(VK_SUCCESS, CreateSwapchainHelper(surface, format, usage, &swapchain));
      destroy_swapchain_khr_(vk_device_, swapchain, nullptr);
    }

    vkDestroySurfaceKHR(vk_instance_, surface, nullptr);
  }

  VkInstance vk_instance_;
  VkDevice vk_device_;
  VkDebugUtilsMessengerEXT messenger_cb_ = nullptr;
  PFN_vkCreateSwapchainKHR create_swapchain_khr_;
  PFN_vkDestroySwapchainKHR destroy_swapchain_khr_;
  PFN_vkGetSwapchainImagesKHR get_swapchain_images_khr_;
  PFN_vkAcquireNextImageKHR acquire_next_image_khr_;
  PFN_vkQueuePresentKHR queue_present_khr_;
  PFN_vkGetDeviceQueue2 get_device_queue2_;
  std::unique_ptr<FakeImagePipe> imagepipe_;

  const bool protected_memory_ = false;
  bool init_ = false;
  bool protected_memory_is_supported_ = false;
};

class SwapchainTest : public ::testing::TestWithParam<bool /* protected_memory */> {};

TEST_P(SwapchainTest, Surface) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.Surface(false);
}

TEST_P(SwapchainTest, SurfaceDynamicSymbol) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.Surface(true);
}

TEST_P(SwapchainTest, Create) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain(1, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

TEST_P(SwapchainTest, CreateTwice) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain(2, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

TEST_P(SwapchainTest, CreateForStorage) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain(1, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT);
}

TEST_P(SwapchainTest, CreateForRgbaStorage) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain(1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT);
}

TEST_P(SwapchainTest, CreateForSrgb) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  test.CreateSwapchain(1, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

TEST_P(SwapchainTest, AcquireFence) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  zx::channel endpoint0, endpoint1;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &endpoint0, &endpoint1));

  // Create FakeImagePipe that can consume BuffercollectionToken.
  test.imagepipe_ = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(endpoint1)),
      /*should_present=*/true);

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = endpoint0.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  EXPECT_EQ(VK_SUCCESS,
            test.CreateSwapchainHelper(surface, VK_FORMAT_R8G8B8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &swapchain));

  VkFence fence;
  VkFenceCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
  vkCreateFence(test.vk_device_, &info, nullptr, &fence);

  uint32_t image_index;
  EXPECT_EQ(VK_ERROR_DEVICE_LOST,
            test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE, fence,
                                         &image_index));
  vkDestroyFence(test.vk_device_, fence, nullptr);

  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);
}

INSTANTIATE_TEST_SUITE_P(SwapchainTestSuite, SwapchainTest, ::testing::Bool());

class SwapchainFidlTest : public ::testing::TestWithParam<bool /* protected_memory */> {};

TEST_P(SwapchainFidlTest, PresentAndAcquireNoSemaphore) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  std::unique_ptr<FakeImagePipe> imagepipe_ = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(remote_endpoint)),
      /*should_present=*/true);

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS,
            test.CreateSwapchainHelper(surface, VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &swapchain));

  VkQueue queue;
  if (protected_memory) {
    VkDeviceQueueInfo2 queue_info2 = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                                      .pNext = nullptr,
                                      .flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
                                      .queueFamilyIndex = 0,
                                      .queueIndex = 0};
    test.get_device_queue2_(test.vk_device_, &queue_info2, &queue);
  } else {
    vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);
  }

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

  ASSERT_DEATH(test.acquire_next_image_khr_(test.vk_device_, swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                            VK_NULL_HANDLE, &image_index),
               ".*Currently all images are pending.*");

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

TEST_P(SwapchainFidlTest, ForceQuit) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  std::unique_ptr<FakeImagePipe> imagepipe_ = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(remote_endpoint)),
      /*should_present=*/true);

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS,
            test.CreateSwapchainHelper(surface, VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &swapchain));

  VkQueue queue;
  if (protected_memory) {
    VkDeviceQueueInfo2 queue_info2 = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                                      .pNext = nullptr,
                                      .flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
                                      .queueFamilyIndex = 0,
                                      .queueIndex = 0};
    test.get_device_queue2_(test.vk_device_, &queue_info2, &queue);
  } else {
    vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);
  }

  uint32_t image_index;
  EXPECT_EQ(VK_SUCCESS, test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                                     VK_NULL_HANDLE, &image_index));

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

  ASSERT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));
  imagepipe_.reset();

  test.destroy_swapchain_khr_(test.vk_device_, swapchain, nullptr);
  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);
}

TEST_P(SwapchainFidlTest, AvoidSemaphoreHang) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  std::unique_ptr<FakeImagePipe> imagepipe = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(remote_endpoint)),
      /*should_present=*/false);

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS,
            test.CreateSwapchainHelper(surface, VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &swapchain));

  VkQueue queue;
  if (protected_memory) {
    VkDeviceQueueInfo2 queue_info2 = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                                      .pNext = nullptr,
                                      .flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
                                      .queueFamilyIndex = 0,
                                      .queueIndex = 0};
    test.get_device_queue2_(test.vk_device_, &queue_info2, &queue);
  } else {
    vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);
  }

  uint32_t image_index;
  // Acquire all initial images.
  for (uint32_t i = 0; i < 3; i++) {
    EXPECT_EQ(VK_SUCCESS,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(i, image_index);
  }

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

  present_index = 0;
  ASSERT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));
  present_index = 1;
  ASSERT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));

  VkSemaphoreCreateInfo semaphore_create_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  VkSemaphore semaphore;
  ASSERT_EQ(VK_SUCCESS,
            vkCreateSemaphore(test.vk_device_, &semaphore_create_info, nullptr, &semaphore));

  ASSERT_EQ(VK_SUCCESS, test.acquire_next_image_khr_(test.vk_device_, swapchain, UINT64_MAX,
                                                     semaphore, VK_NULL_HANDLE, &image_index));

  VkPipelineStageFlags wait_flag = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkSubmitInfo info{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &semaphore,
                    .pWaitDstStageMask = &wait_flag};
  ASSERT_EQ(VK_SUCCESS, vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE));

  auto future = std::async(std::launch::async, [imagepipe = std::move(imagepipe)]() mutable {
    // Wait enough time for the DeviceWaitIdle to start waiting on the semaphore, but not enough
    // time to get a lost device.
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
    imagepipe.reset();
  });
  auto acquire_future = std::async(std::launch::async, [&test, &swapchain]() mutable {
    uint32_t image_index;
    // No semaphore or fence, so this should wait on the CPU.
    EXPECT_EQ(VK_ERROR_SURFACE_LOST_KHR,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
  });
  vkDeviceWaitIdle(test.vk_device_);
  // Wait before the queue_present_khr to externally synchronize access to |swapchain|.
  acquire_future.wait();

  present_index = 2;
  EXPECT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));
  EXPECT_EQ(VK_ERROR_SURFACE_LOST_KHR, present_result);

  vkDestroySemaphore(test.vk_device_, semaphore, nullptr);

  test.destroy_swapchain_khr_(test.vk_device_, swapchain, nullptr);
  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);
}

TEST_P(SwapchainFidlTest, AcquireZeroTimeout) {
  const bool protected_memory = GetParam();
  TestSwapchain test(protected_memory);
  if (protected_memory && !test.protected_memory_is_supported_) {
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.init_);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel local_endpoint, remote_endpoint;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &local_endpoint, &remote_endpoint));

  std::unique_ptr<FakeImagePipe> imagepipe = std::make_unique<FakeImagePipe>(
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(remote_endpoint)),
      /*should_present=*/false);

  VkImagePipeSurfaceCreateInfoFUCHSIA create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .imagePipeHandle = local_endpoint.release(),
      .pNext = nullptr,
  };

  VkSurfaceKHR surface;
  EXPECT_EQ(VK_SUCCESS,
            vkCreateImagePipeSurfaceFUCHSIA(test.vk_instance_, &create_info, nullptr, &surface));

  VkSwapchainKHR swapchain;
  ASSERT_EQ(VK_SUCCESS,
            test.CreateSwapchainHelper(surface, VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &swapchain));

  VkQueue queue;
  if (protected_memory) {
    VkDeviceQueueInfo2 queue_info2 = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                                      .pNext = nullptr,
                                      .flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
                                      .queueFamilyIndex = 0,
                                      .queueIndex = 0};
    test.get_device_queue2_(test.vk_device_, &queue_info2, &queue);
  } else {
    vkGetDeviceQueue(test.vk_device_, 0, 0, &queue);
  }

  uint32_t image_index;
  // Acquire all initial images.
  for (uint32_t i = 0; i < 3; i++) {
    EXPECT_EQ(VK_SUCCESS,
              test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &image_index));
    EXPECT_EQ(i, image_index);
  }

  // Timeout of zero with no pending presents
  EXPECT_EQ(VK_NOT_READY,
            test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                         VK_NULL_HANDLE, &image_index));

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

  ASSERT_EQ(VK_SUCCESS, test.queue_present_khr_(queue, &present_info));

  // Timeout of zero with pending presents
  EXPECT_EQ(VK_NOT_READY,
            test.acquire_next_image_khr_(test.vk_device_, swapchain, 0, VK_NULL_HANDLE,
                                         VK_NULL_HANDLE, &image_index));

  // Close the remote end because we've configured it to not-present, and the swapchain
  // teardown hangs otherwise.
  imagepipe.reset();

  test.destroy_swapchain_khr_(test.vk_device_, swapchain, nullptr);
  vkDestroySurfaceKHR(test.vk_instance_, surface, nullptr);
}

INSTANTIATE_TEST_SUITE_P(SwapchainFidlTestSuite, SwapchainFidlTest, ::testing::Bool());
