// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "garnet/lib/ui/gfx/util/vulkan_utils.h"

namespace scenic_impl {
namespace gfx {
namespace test {

std::unique_ptr<SessionForTest> VkSessionTest::CreateSession() {
  SessionContext session_context = CreateBarebonesSessionContext();

  // Initialize Vulkan.
  escher::VulkanInstance::Params instance_params(
      {{}, {VK_EXT_DEBUG_REPORT_EXTENSION_NAME}, false});

  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
  auto vulkan_instance =
      escher::VulkanInstance::New(std::move(instance_params));
  auto vulkan_device = escher::VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME}, vk::SurfaceKHR()});

  escher_ = std::make_unique<escher::Escher>(vulkan_device);
  release_fence_signaller_ = std::make_unique<escher::ReleaseFenceSignaller>(
      escher_->command_buffer_sequencer());
  image_factory_ = std::make_unique<escher::ImageFactoryAdapter>(
      escher_->gpu_allocator(), escher_->resource_recycler());

  session_context.vk_device = escher_->vk_device();
  session_context.escher = escher_.get();
  session_context.imported_memory_type_index = GetImportedMemoryTypeIndex(
      escher_->vk_physical_device(), escher_->vk_device());
  session_context.escher_resource_recycler = escher_->resource_recycler();
  session_context.escher_image_factory = image_factory_.get();
  session_context.release_fence_signaller = release_fence_signaller_.get();

  return std::make_unique<SessionForTest>(1, std::move(session_context), this,
                                          error_reporter());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
