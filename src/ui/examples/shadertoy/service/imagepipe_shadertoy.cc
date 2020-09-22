// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/shadertoy/service/imagepipe_shadertoy.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/trace/event.h>

#include "src/ui/examples/shadertoy/service/renderer.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/framebuffer.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image.h"

namespace shadertoy {

ShadertoyStateForImagePipe::ShadertoyStateForImagePipe(
    App* app, fidl::InterfaceHandle<fuchsia::images::ImagePipe2> image_pipe)
    : ShadertoyState(app), image_pipe_(image_pipe.Bind()) {
  image_pipe_.set_error_handler([this](zx_status_t status) { this->Close(); });
}

ShadertoyStateForImagePipe::~ShadertoyStateForImagePipe() = default;

void ShadertoyStateForImagePipe::ClearFramebuffers() {
  for (size_t i = 0; i < kNumFramebuffers; ++i) {
    auto& fb = framebuffers_[i];
    fb.framebuffer = nullptr;
    fb.acquire_semaphore = nullptr;
    fb.release_semaphore = nullptr;
    fb.acquire_fence.reset();
    fb.release_fence.reset();
    if (fb.image_pipe_id) {
      // TODO(fxbug.dev/23488): The docs in image_pipe.fidl says that all release fences
      // must "be signaled before freeing or modifying the underlying memory
      // object".  However, it seems convenient to allow clients to free the
      // object immediately; this shouldn't be a problem because the
      // presentation queue also has a reference to the memory.
      image_pipe_->RemoveImage(fb.image_pipe_id);
      fb.image_pipe_id = 0;
    }
  }
}

void ShadertoyStateForImagePipe::OnSetResolution() {
  ClearFramebuffers();

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Sysmem connection failed";
    Close();
    return;
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "AllocateSharedCollection failed";
    Close();
    return;
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Duplicate failed";
    Close();
    return;
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr scenic_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Duplicate failed";
    Close();
    return;
  }
  status = local_token->Sync();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Sync failed";
    Close();
    return;
  }

  // Use |scenic_token| to collect image pipe constraints.
  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(scenic_token));

  // Use |vulkan_token| to set vulkan constraints.
  auto vk_device = escher()->vk_device();
  auto vk_loader = escher()->device()->dispatch_loader();

  escher::ImageInfo escher_image_info;
  escher_image_info.format = renderer()->framebuffer_format();
  escher_image_info.width = width();
  escher_image_info.height = height();
  escher_image_info.sample_count = 1;
  escher_image_info.usage = vk::ImageUsageFlagBits::eColorAttachment;

  vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
  buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
  auto create_buffer_collection_result =
      vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader);
  if (create_buffer_collection_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkCreateBufferCollectionFUCHSIA failed: "
                   << vk::to_string(create_buffer_collection_result.result);
    Close();
    return;
  }
  vk::ImageCreateInfo image_create_info =
      escher::image_utils::CreateVkImageCreateInfo(escher_image_info, vk::ImageLayout::eUndefined);
  auto constraints_result = vk_device.setBufferCollectionConstraintsFUCHSIA(
      create_buffer_collection_result.value, image_create_info, vk_loader);
  if (constraints_result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkSetBufferCollectionConstraints failed: "
                   << vk::to_string(constraints_result);
    Close();
    return;
  }
  vk::BufferCollectionFUCHSIA buffer_collection_fuchsia = create_buffer_collection_result.value;

  // Use |local_token| to set buffer count.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "BindSharedCollection failed:" << status;
    Close();
    return;
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = kNumFramebuffers;
  constraints.usage.none = fuchsia::sysmem::noneUsage;
  status = buffer_collection->SetConstraints(true /* has_constraints */, constraints);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "SetConstraints failed:" << status;
    Close();
    return;
  }

  // Wait for buffers to be allocated.
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  zx_status_t allocation_status = ZX_OK;
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(ERROR) << "WaitForBuffersAllocated failed:" << status;
    Close();
    return;
  }

  // Create images and framebuffers from buffer collection.
  for (size_t i = 0; i < kNumFramebuffers; ++i) {
    auto& fb = framebuffers_[i];

    auto acquire_semaphore_pair = escher::NewSemaphoreEventPair(escher());
    auto release_semaphore_pair = escher::NewSemaphoreEventPair(escher());
    if (!acquire_semaphore_pair.first || !release_semaphore_pair.first) {
      FX_LOGS(ERROR) << "Semaphore failed.";
      ClearFramebuffers();
      Close();
      return;
    }

    // The release fences should be immediately ready to render, since they are
    // passed to DrawFrame() as the 'framebuffer_ready' semaphore.
    release_semaphore_pair.second.signal(0u, escher::kFenceSignalled);

    // Create vkImage.
    vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
    collection_image_info.collection = buffer_collection_fuchsia;
    collection_image_info.index = i;
    image_create_info.setPNext(&collection_image_info);
    auto image_result = vk_device.createImage(image_create_info);
    if (image_result.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "VkCreateImage failed: " << vk::to_string(image_result.result);
      ClearFramebuffers();
      Close();
      return;
    }
    vk::Image image = image_result.value;

    // Import memory from buffer collection.
    auto collection_properties =
        vk_device.getBufferCollectionPropertiesFUCHSIA(buffer_collection_fuchsia, vk_loader);
    if (collection_properties.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "VkGetBufferCollectionProperties failed: "
                     << vk::to_string(collection_properties.result);
      ClearFramebuffers();
      Close();
      return;
    }
    auto memory_requirements = vk_device.getImageMemoryRequirements(image);
    const uint32_t memory_type_index = escher::CountTrailingZeros(
        memory_requirements.memoryTypeBits & collection_properties.value.memoryTypeBits);
    vk::ImportMemoryBufferCollectionFUCHSIA import_info;
    import_info.collection = buffer_collection_fuchsia;
    import_info.index = i;
    vk::MemoryAllocateInfo alloc_info;
    alloc_info.setPNext(&import_info);
    alloc_info.memoryTypeIndex = memory_type_index;
    alloc_info.allocationSize = memory_requirements.size;
    auto allocate_memory_result = vk_device.allocateMemory(alloc_info);
    if (allocate_memory_result.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "VkAllocateMemory failed.";
      ClearFramebuffers();
      Close();
      return;
    }
    auto memory =
        escher::GpuMem::AdoptVkMemory(vk_device, allocate_memory_result.value,
                                      memory_requirements.size, false /* needs_mapped_ptr */);

    // Create framebuffer.
    auto escher_image =
        escher::impl::NaiveImage::AdoptVkImage(escher()->resource_recycler(), escher_image_info,
                                               image, memory, vk::ImageLayout::eUndefined);
    fb.framebuffer = fxl::MakeRefCounted<escher::Framebuffer>(
        escher(), width(), height(), std::vector<escher::ImagePtr>{escher_image},
        renderer()->render_pass());
    fb.acquire_semaphore = std::move(acquire_semaphore_pair.first);
    fb.release_semaphore = std::move(release_semaphore_pair.first);
    fb.acquire_fence = std::move(acquire_semaphore_pair.second);
    fb.release_fence = std::move(release_semaphore_pair.second);
    fb.image_pipe_id = next_image_pipe_id_++;

    // Add image to |image_pipe_|.
    fuchsia::sysmem::ImageFormat_2 image_format = {};
    image_format.coded_width = width();
    image_format.coded_height = height();
    image_pipe_->AddImage(fb.image_pipe_id, kBufferId, i, image_format);
  }

  vk_device.destroyBufferCollectionFUCHSIA(buffer_collection_fuchsia, nullptr, vk_loader);
  buffer_collection->Close();
}

static zx::event DuplicateEvent(const zx::event& evt) {
  zx::event dup;
  auto result = evt.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (result != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate event (status: " << result << ").";
  }
  return dup;
}

void ShadertoyStateForImagePipe::DrawFrame(uint64_t presentation_time, float animation_time) {
  TRACE_DURATION("gfx", "ShadertoyStateForImagePipe::DrawFrame");
  // Prepare arguments.
  auto& fb = framebuffers_[next_framebuffer_index_];
  next_framebuffer_index_ = (next_framebuffer_index_ + 1) % kNumFramebuffers;
  zx::event acquire_fence(DuplicateEvent(fb.acquire_fence));
  zx::event release_fence(DuplicateEvent(fb.release_fence));
  if (!acquire_fence || !release_fence) {
    Close();
    return;
  }

  // Render.
  Renderer::Params params;
  params.iResolution = glm::vec3(width(), height(), 1);
  params.iTime = animation_time;
  // TODO(fxbug.dev/23487):  params.iTimeDelta = ??;
  // TODO(fxbug.dev/23487): params.iFrame = 0;
  // TODO(fxbug.dev/23487): params.iChannelTime = ??;
  // TODO(fxbug.dev/23487): params.iChannelResolution = ??;
  params.iMouse = i_mouse();
  // TODO(fxbug.dev/23487): params.iDate = ??;
  // TODO(fxbug.dev/23487): params.iSampleRate = ??;

  renderer()->DrawFrame(fb.framebuffer, pipeline(), params, channel0(), channel1(), channel2(),
                        channel3(), fb.release_semaphore, fb.acquire_semaphore);

  // Present the image and request another frame.
  auto present_image_callback =
      [weak = weak_ptr_factory()->GetWeakPtr()](fuchsia::images::PresentationInfo info) {
        // Need this cast in order to call protected member of superclass.
        if (auto self = static_cast<ShadertoyStateForImagePipe*>(weak.get())) {
          self->OnFramePresented(std::move(info));
        }
      };

  std::vector<zx::event> acquire_fences;
  acquire_fences.push_back(std::move(acquire_fence));
  std::vector<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));
  TRACE_FLOW_BEGIN("gfx", "image_pipe_present_image", fb.image_pipe_id);
  image_pipe_->PresentImage(fb.image_pipe_id, presentation_time, std::move(acquire_fences),
                            std::move(release_fences), present_image_callback);
}

}  // namespace shadertoy
