// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/common/demo_harness.h"

#include <lib/syslog/cpp/macros.h>

#include <iostream>
#include <set>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/examples/escher/common/demo.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/lib/escher/vk/vulkan_instance.h"

#define VK_CHECK_RESULT(XXX) FX_CHECK(XXX.result == vk::Result::eSuccess)

static constexpr uint64_t kLogGpuTimestampsEveryNFrames = 200;

DemoHarness::DemoHarness(WindowParams window_params) : window_params_(window_params) {
  // Init() is called by DemoHarness::New().
}

DemoHarness::~DemoHarness() { FX_DCHECK(shutdown_complete_); }

void DemoHarness::Init(InstanceParams instance_params) {
  FX_LOGS(INFO) << "Initializing " << window_params_.window_name
                << (window_params_.use_fullscreen ? " (fullscreen " : " (windowed ")
                << window_params_.width << "x" << window_params_.height << ")";
  InitWindowSystem();
  CreateInstance(std::move(instance_params));
  vk::SurfaceKHR surface = CreateWindowAndSurface(window_params_);

  std::set<std::string> device_extension_names;
  AppendPlatformSpecificDeviceExtensionNames(&device_extension_names);
  CreateDeviceAndQueue({std::move(device_extension_names),
                        {VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME},
                        surface});

  escher::GlslangInitializeProcess();
  CreateEscher();

  CreateSwapchain();
}

void DemoHarness::Shutdown() {
  FX_DCHECK(!shutdown_complete_);
  shutdown_complete_ = true;

  escher()->vk_device().waitIdle();
  escher()->Cleanup();

  DestroySwapchain();

  escher::GlslangFinalizeProcess();
  DestroyEscher();

  DestroyDevice();
  DestroyInstance();
  ShutdownWindowSystem();
}

void DemoHarness::CreateInstance(InstanceParams params) {
  TRACE_DURATION("gfx", "DemoHarness::CreateInstance");

  // Add our own required layers and extensions in addition to those provided
  // by the caller.  Verify that they are all available, and obtain info about
  // them that is used:
  // - to create the instance.
  // - for future reference.
  AppendPlatformSpecificInstanceExtensionNames(&params);

  // We need this extension for getting debug callbacks.
  params.extension_names.insert("VK_EXT_debug_report");

  instance_ = escher::VulkanInstance::New(std::move(params));
  FX_CHECK(instance_);

  instance_->RegisterDebugReportCallback(RedirectDebugReport, this);
}

void DemoHarness::CreateDeviceAndQueue(escher::VulkanDeviceQueues::Params params) {
  TRACE_DURATION("gfx", "DemoHarness::CreateDeviceAndQueue");
  device_queues_ = escher::VulkanDeviceQueues::New(instance_, std::move(params));
}

void DemoHarness::CreateSwapchain() {
  TRACE_DURATION("gfx", "DemoHarness::CreateSwapchain");

  FX_CHECK(!swapchain_.swapchain);
  FX_CHECK(swapchain_.images.empty());

  vk::SurfaceCapabilitiesKHR surface_caps;
  {
    auto result = physical_device().getSurfaceCapabilitiesKHR(surface());
    VK_CHECK_RESULT(result);
    surface_caps = std::move(result.value);
  }

  std::vector<vk::PresentModeKHR> present_modes;
  {
    auto result = physical_device().getSurfacePresentModesKHR(surface());
    VK_CHECK_RESULT(result);
    present_modes = std::move(result.value);
  }

  // TODO: handle undefined width/height.
  vk::Extent2D swapchain_extent = surface_caps.currentExtent;
  constexpr uint32_t VK_UNDEFINED_WIDTH_OR_HEIGHT = 0xFFFFFFFF;
  if (swapchain_extent.width == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.width = window_params_.width;
  }
  if (swapchain_extent.height == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.height = window_params_.height;
  }

  if (swapchain_extent.width != window_params_.width ||
      swapchain_extent.height != window_params_.height) {
    window_params_.width = swapchain_extent.width;
    window_params_.height = swapchain_extent.height;
  }
  FX_CHECK(swapchain_extent.width == window_params_.width);
  FX_CHECK(swapchain_extent.height == window_params_.height);

  // FIFO mode is always available, but we will try to find a more efficient
  // mode.
  vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;
// TODO: Find out why these modes are causing lower performance on Skylake
#if 0
  for (auto& mode : present_modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      // Best choice: lowest-latency non-tearing mode.
      swapchain_present_mode = vk::PresentModeKHR::eMailbox;
      break;
    }
    if (mode == vk::PresentModeKHR::eImmediate) {
      // Satisfactory choice: fastest, but tears.
      swapchain_present_mode = vk::PresentModeKHR::eImmediate;
    }
  }
#endif

  // Determine number of images in the swapchain.
  swapchain_image_count_ = window_params_.desired_swapchain_image_count;
  if (surface_caps.minImageCount > swapchain_image_count_) {
    swapchain_image_count_ = surface_caps.minImageCount;
  } else if (surface_caps.maxImageCount < swapchain_image_count_ &&
             surface_caps.maxImageCount != 0) {  // 0 means "no limit"
    swapchain_image_count_ = surface_caps.maxImageCount;
  }

  // TODO: choosing an appropriate pre-transform will probably be important on
  // mobile devices.
  auto pre_transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

  // Pick a format and color-space for the swap-chain.
  vk::Format format = vk::Format::eUndefined;
  vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
  {
    auto result = physical_device().getSurfaceFormatsKHR(surface());
    VK_CHECK_RESULT(result);
    for (auto& sf : result.value) {
      if (sf.colorSpace != color_space)
        continue;

      // TODO: remove this once Magma supports SRGB swapchains.
      if (sf.format == vk::Format::eB8G8R8A8Unorm) {
        format = sf.format;
        break;
      }

      if (sf.format == vk::Format::eB8G8R8A8Srgb) {
        // eB8G8R8A8Srgb is our favorite!
        format = sf.format;
        break;
      } else if (format == vk::Format::eUndefined) {
        // Anything is better than eUndefined.
        format = sf.format;
      }
    }
  }
  FX_CHECK(format != vk::Format::eUndefined);

  // TODO: old_swapchain will come into play (I think) when we support
  // resizing the window.
  vk::SwapchainKHR old_swapchain = nullptr;

  // Using eTransferDst allows us to blit debug info onto the surface.
  // Using eSampled allows us to save memory by using the color attachment
  // for intermediate computation.
  const vk::ImageUsageFlags kImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment |
                                               vk::ImageUsageFlagBits::eTransferDst |
                                               vk::ImageUsageFlagBits::eSampled;

  // Create the swapchain.
  vk::SwapchainKHR swapchain;
  {
    vk::SwapchainCreateInfoKHR info;
    info.surface = surface();
    info.minImageCount = swapchain_image_count_;
    info.imageFormat = format;
    info.imageColorSpace = color_space;
    info.imageExtent = swapchain_extent;
    info.imageArrayLayers = 1;  // TODO: what is this?
    info.imageUsage = kImageUsageFlags;
    info.queueFamilyIndexCount = 1;
    uint32_t queue_family_index = device_queues_->vk_main_queue_family();
    info.pQueueFamilyIndices = &queue_family_index;
    info.preTransform = pre_transform;
    info.presentMode = swapchain_present_mode;
    info.oldSwapchain = old_swapchain;
    info.clipped = true;

    auto result = device().createSwapchainKHR(info);
    VK_CHECK_RESULT(result);
    swapchain = result.value;
  }

  if (old_swapchain) {
    // Note: destroying the swapchain also cleans up all its associated
    // presentable images once the platform is done with them.
    device().destroySwapchainKHR(old_swapchain);
  }

  // Obtain swapchain images and buffers.
  {
    auto result = device().getSwapchainImagesKHR(swapchain);
    VK_CHECK_RESULT(result);

    std::vector<vk::Image> images(std::move(result.value));
    std::vector<escher::ImagePtr> escher_images;
    escher_images.reserve(images.size());
    for (auto& im : images) {
      escher::ImageInfo image_info;
      image_info.format = format;
      image_info.width = swapchain_extent.width;
      image_info.height = swapchain_extent.height;
      image_info.usage = kImageUsageFlags;

      auto escher_image = escher::Image::WrapVkImage(escher()->resource_recycler(), image_info, im,
                                                     vk::ImageLayout::eUndefined);
      FX_CHECK(escher_image);
      escher_images.push_back(escher_image);
    }
    swapchain_ = escher::VulkanSwapchain(swapchain, escher_images, swapchain_extent.width,
                                         swapchain_extent.height, format, color_space);
  }

  // Create swapchain helper.
  swapchain_helper_ = std::make_unique<escher::VulkanSwapchainHelper>(swapchain_, device(),
                                                                      GetVulkanContext().queue);
}

void DemoHarness::CreateEscher() {
  TRACE_DURATION("gfx", "DemoHarness::CreateEscher");

  FX_CHECK(!escher_);
  escher_ = std::make_unique<escher::Escher>(device_queues_, filesystem_);

  // Replace Escher's default pipeline builder.
  {
    std::string vk_pipeline_cache_path = GetCacheDirectoryPath() + "/vk_pipeline_cache";
    std::vector<uint8_t> initial_bytes;
    files::ReadFileToVector(vk_pipeline_cache_path, &initial_bytes);

    auto pipeline_builder = std::make_unique<escher::PipelineBuilder>(
        device(), initial_bytes.empty() ? nullptr : initial_bytes.data(), initial_bytes.size(),
        [path = std::move(vk_pipeline_cache_path)](std::vector<uint8_t> bytes) {
          if (!files::WriteFile(path, reinterpret_cast<char*>(bytes.data()), bytes.size())) {
            FX_LOGS(WARNING) << "Failed to write " << bytes.size()
                             << " bytes to VkPipelineCache data file: " << path;
          }
        });

    // DemoHarness expects pipeline creation to always be done at well-defined times (typically at
    // startup).  The program will crash if there is an attempt to unexpectedly lazily generate a
    // pipeline.  A less draconian approach will be desirable for some other applications, such as
    // Scenic.
    pipeline_builder->set_log_pipeline_creation_callback(
        [](const vk::GraphicsPipelineCreateInfo* graphics_info,
           const vk::ComputePipelineCreateInfo* compute_info) {
          if (graphics_info) {
            FX_CHECK(false) << "attempted to lazily generate a Vulkan graphics pipeline.";
          } else {
            FX_CHECK(false) << "attempted to lazily generate a Vulkan compute pipeline.";
          }
        });

    escher_->set_pipeline_builder(std::move(pipeline_builder));

    // Ensure that the cache directory exists.
    if (!files::IsDirectory(GetCacheDirectoryPath())) {
      if (!files::CreateDirectory(GetCacheDirectoryPath())) {
        FX_LOGS(WARNING) << "Failed to create cache directory: " << GetCacheDirectoryPath();
      }
    }
  }
}

void DemoHarness::DestroyEscher() { escher_.reset(); }

void DemoHarness::DestroySwapchain() {
  swapchain_helper_.reset();

  swapchain_.images.clear();

  FX_CHECK(swapchain_.swapchain);
  device().destroySwapchainKHR(swapchain_.swapchain);
  swapchain_.swapchain = nullptr;
}

void DemoHarness::DestroyDevice() {
  if (auto surf = surface()) {
    instance().destroySurfaceKHR(surf);
  }
  device_queues_ = nullptr;
}

void DemoHarness::DestroyInstance() { instance_ = nullptr; }

VkBool32 DemoHarness::HandleDebugReport(VkDebugReportFlagsEXT flags_in,
                                        VkDebugReportObjectTypeEXT object_type_in, uint64_t object,
                                        size_t location, int32_t message_code,
                                        const char* pLayerPrefix, const char* pMessage) {
  vk::DebugReportFlagsEXT flags(static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

  bool fatal = false;

// Macro to facilitate matching messages.  Example usage:
//  if (MATCH_REPORT(DescriptorSet, 0, "VUID-VkWriteDescriptorSet-descriptorType-01403")) {
//    FX_LOGS(INFO) << "ignoring descriptor set problem: " << pMessage << "\n\n";
//    return false;
//  }
#define MATCH_REPORT(OTYPE, CODE, X)                                                    \
  ((object_type == vk::DebugReportObjectTypeEXT::e##OTYPE) && (message_code == CODE) && \
   (0 == strncmp(pMessage + 3, X, strlen(X) - 1)))

  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    // Paranoid check that there aren't multiple flags.
    FX_DCHECK(flags == vk::DebugReportFlagBitsEXT::eInformation);

    std::cerr << "## Vulkan Information: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    std::cerr << "## Vulkan Warning: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    std::cerr << "## Vulkan Performance Warning: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eError) {
    // Treat all errors as fatal.
    fatal = true;
    std::cerr << "## Vulkan Error: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eDebug) {
    std::cerr << "## Vulkan Debug: ";
  } else {
    // This should never happen, unless a new value has been added to
    // vk::DebugReportFlagBitsEXT.  In that case, add a new if-clause above.
    fatal = true;
    std::cerr << "## Vulkan Unknown Message Type (flags: " << vk::to_string(flags) << "): ";
  }

  std::cerr << pMessage << " (layer: " << pLayerPrefix << "  code: " << message_code
            << "  object-type: " << vk::to_string(object_type) << "  object: " << object
            << "  location: " << location << ")" << std::endl;

  // Crash immediately on fatal errors.
  FX_CHECK(!fatal);

  return false;
}

escher::VulkanContext DemoHarness::GetVulkanContext() { return device_queues_->GetVulkanContext(); }

bool DemoHarness::MaybeDrawFrame() {
  static constexpr size_t kOffscreenBenchmarkFrameCount = 1000;

  if (run_offscreen_benchmark_) {
    TRACE_DURATION("gfx", "escher::DemoHarness::MaybeDrawFrame (benchmarking)");

    run_offscreen_benchmark_ = false;

    Demo::RunOffscreenBenchmark(demo_, swapchain_.width, swapchain_.height, swapchain_.format,
                                kOffscreenBenchmarkFrameCount);

    // Guarantee that there are no frames in flight.
    escher()->vk_device().waitIdle();
    FX_CHECK(escher()->Cleanup());
    outstanding_frames_ = 0;
  }

  if (IsAtMaxOutstandingFrames()) {
    // Try clean up; maybe a frame is actually already finished.
    escher()->Cleanup();
    if (IsAtMaxOutstandingFrames()) {
      // Still too many frames in flight.  Try again later.
      return false;
    }
  }

  {
    TRACE_DURATION("gfx", "escher::DemoHarness::MaybeDrawFrame (drawing)", "frame", frame_count_);

    auto frame = escher()->NewFrame(demo_->name(), frame_count_, enable_gpu_logging_);
    OnFrameCreated();

    swapchain_helper_->DrawFrame([&, this](const escher::ImagePtr& output_image,
                                           const escher::SemaphorePtr& framebuffer_acquired,
                                           const escher::SemaphorePtr& render_finished) {
      if (output_image->layout() != output_image->swapchain_layout()) {
        // No need to synchronize, because the entire command buffer is synchronized via
        // |framebuffer_acquired|.  Would be nice to roll this barrier into |swapchain_helper_|,
        // but then it would need to know about the command buffer, which may not be desirable.
        frame->cmds()->ImageBarrier(output_image, output_image->layout(),
                                    output_image->swapchain_layout(),
                                    vk::PipelineStageFlagBits::eBottomOfPipe, vk::AccessFlags(),
                                    vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlags());
      }

      demo_->DrawFrame(frame, output_image, framebuffer_acquired);
      frame->EndFrame(render_finished, [this]() { OnFrameDestroyed(); });
    });
  }

  if (++frame_count_ == 1) {
    first_frame_microseconds_ = stopwatch_.GetElapsedMicroseconds();
    stopwatch_.Reset();
  } else if (frame_count_ % kLogGpuTimestampsEveryNFrames == 0) {
    enable_gpu_logging_ = true;

    // Print out FPS and memory stats.
    FX_LOGS(INFO) << "---- Average frame rate: " << ComputeFps();
    FX_LOGS(INFO) << "---- Total GPU memory: " << (escher()->GetNumGpuBytesAllocated() / 1024)
                  << "kB";
  } else {
    enable_gpu_logging_ = false;
  }

  escher()->Cleanup();
  return true;
}

bool DemoHarness::HandleKeyPress(std::string key) {
  if (key == "ESCAPE") {
    SetShouldQuit();
    return true;
  }
  if (demo_) {
    return demo_->HandleKeyPress(std::move(key));
  }
  return false;
}

bool DemoHarness::IsAtMaxOutstandingFrames() {
  FX_DCHECK(outstanding_frames_ <= Demo::kMaxOutstandingFrames);
  return outstanding_frames_ >= Demo::kMaxOutstandingFrames;
}

void DemoHarness::OnFrameCreated() {
  FX_DCHECK(!IsAtMaxOutstandingFrames());
  ++outstanding_frames_;
}

void DemoHarness::OnFrameDestroyed() {
  FX_DCHECK(outstanding_frames_ > 0);
  --outstanding_frames_;
  FX_DCHECK(!IsAtMaxOutstandingFrames());
}

void DemoHarness::Run(Demo* demo) {
  BeginRun(demo);
  RunForPlatform(demo);
  EndRun();
}

void DemoHarness::BeginRun(Demo* demo) {
  FX_CHECK(demo);
  FX_CHECK(!demo_);
  demo_ = demo;
  frame_count_ = 0;
  first_frame_microseconds_ = 0;
  stopwatch_.Reset();
}

void DemoHarness::EndRun() {
  FX_CHECK(demo_);
  demo_ = nullptr;

  FX_LOGS(INFO) << "Average frame rate: " << ComputeFps();
  FX_LOGS(INFO) << "First frame took: " << first_frame_microseconds_ / 1000.0 << " milliseconds";
  escher()->Cleanup();
}

double DemoHarness::ComputeFps() {
  // Omit the first frame when computing the average, because it is generating
  // pipelines.  We subtract 2 instead of 1 because we just incremented it in
  // DrawFrame().
  //
  // TODO(fxbug.dev/7248): This could be improved.  For example, when called from the
  // destructor we don't know how much time has elapsed since the last
  // DrawFrame(); it might be more accurate to subtract 1 instead of 2.  Also,
  // on Linux the swapchain allows us to queue up many DrawFrame() calls so if
  // we quit after a short time then the FPS will be artificially high.
  auto microseconds = stopwatch_.GetElapsedMicroseconds();
  return (frame_count_ - 2) * 1000000.0 / (microseconds - first_frame_microseconds_);
}
