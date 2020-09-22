// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/escher.h"

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/frame_manager.h"
#if ESCHER_USE_RUNTIME_GLSL
#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"  // nogncheck
#endif
#include "src/ui/lib/escher/impl/image_cache.h"
#include "src/ui/lib/escher/impl/mesh_manager.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/profiling/timestamp_profiler.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/buffer_cache.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/sampler_cache.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"
#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator.h"
#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator_cache.h"
#include "src/ui/lib/escher/vk/impl/framebuffer_allocator.h"
#include "src/ui/lib/escher/vk/impl/pipeline_layout_cache.h"
#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/lib/escher/vk/texture.h"
#include "src/ui/lib/escher/vk/vma_gpu_allocator.h"

namespace escher {

namespace {

// Constructor helper.
std::unique_ptr<impl::CommandBufferPool> NewCommandBufferPool(
    const VulkanContext& context, impl::CommandBufferSequencer* sequencer,
    bool use_protected_memory) {
  return std::make_unique<impl::CommandBufferPool>(
      context.device, context.queue, context.queue_family_index, sequencer,
      /*supports_graphics_and_compute=*/true, use_protected_memory);
}

// Constructor helper.
std::unique_ptr<impl::CommandBufferPool> NewTransferCommandBufferPool(
    const VulkanContext& context, impl::CommandBufferSequencer* sequencer,
    bool use_protected_memory) {
  if (!context.transfer_queue) {
    return nullptr;
  } else {
    return std::make_unique<impl::CommandBufferPool>(
        context.device, context.transfer_queue, context.transfer_queue_family_index, sequencer,
        /*supports_graphics_and_compute=*/false, use_protected_memory);
  }
}

// Constructor helper.
std::unique_ptr<impl::MeshManager> NewMeshManager(impl::CommandBufferPool* main_pool,
                                                  impl::CommandBufferPool* transfer_pool,
                                                  GpuAllocator* allocator,
                                                  ResourceRecycler* resource_recycler) {
  return std::make_unique<impl::MeshManager>(transfer_pool ? transfer_pool : main_pool, allocator,
                                             resource_recycler);
}

}  // anonymous namespace

// This version exists because if we were to define default args in the constructor (e.g.
// "std::unique_ptr<PipelineBuilder> pipeline_builder = nullptr"), then we would run into
// compile problems that can be solved by either:
//   - this approach
//   - including additional headers in escher.h, instead of forward-declaring
// Since many files include escher.h, it is worthwhile to forward-declare as much as possible.
Escher::Escher(VulkanDeviceQueuesPtr device) : Escher(std::move(device), HackFilesystem::New()) {}

Escher::Escher(VulkanDeviceQueuesPtr device, HackFilesystemPtr filesystem)
    : device_(std::move(device)),
      vulkan_context_(device_->GetVulkanContext()),
      gpu_allocator_(std::make_unique<VmaGpuAllocator>(vulkan_context_)),
      command_buffer_sequencer_(std::make_unique<impl::CommandBufferSequencer>()),
      command_buffer_pool_(NewCommandBufferPool(vulkan_context_, command_buffer_sequencer_.get(),
                                                /*use_protected_memory=*/false)),
      transfer_command_buffer_pool_(NewTransferCommandBufferPool(
          vulkan_context_, command_buffer_sequencer_.get(), /*use_protected_memory=*/false)),
#if ESCHER_USE_RUNTIME_GLSL
      shaderc_compiler_(std::make_unique<shaderc::Compiler>()),
#endif
      pipeline_builder_(std::make_unique<PipelineBuilder>(device_->vk_device())),
      weak_factory_(this) {
  FX_DCHECK(vulkan_context_.instance);
  FX_DCHECK(vulkan_context_.physical_device);
  FX_DCHECK(vulkan_context_.device);
  FX_DCHECK(vulkan_context_.queue);
  // TODO: additional validation, e.g. ensure that queue supports both graphics
  // and compute.

  // Initialize instance variables that require |weak_factory_| to already have
  // been initialized.
  resource_recycler_ = std::make_unique<ResourceRecycler>(GetWeakPtr());
  image_cache_ = std::make_unique<impl::ImageCache>(GetWeakPtr(), gpu_allocator());
  buffer_cache_ = std::make_unique<BufferCache>(GetWeakPtr());
  sampler_cache_ = std::make_unique<SamplerCache>(resource_recycler_->GetWeakPtr());
  mesh_manager_ = NewMeshManager(command_buffer_pool(), transfer_command_buffer_pool(),
                                 gpu_allocator(), resource_recycler());
  descriptor_set_allocator_cache_ =
      std::make_unique<impl::DescriptorSetAllocatorCache>(vk_device());
  pipeline_layout_cache_ = std::make_unique<impl::PipelineLayoutCache>(resource_recycler());
  render_pass_cache_ = std::make_unique<impl::RenderPassCache>(resource_recycler());
  framebuffer_allocator_ =
      std::make_unique<impl::FramebufferAllocator>(resource_recycler(), render_pass_cache_.get());
  image_view_allocator_ = std::make_unique<ImageViewAllocator>(resource_recycler());
  shader_program_factory_ =
      std::make_unique<DefaultShaderProgramFactory>(GetWeakPtr(), std::move(filesystem));

  frame_manager_ = std::make_unique<impl::FrameManager>(GetWeakPtr());

  semaphore_chain_ = std::make_unique<ChainedSemaphoreGenerator>(vk_device());

  // Query relevant Vulkan properties.
  auto device_properties = vk_physical_device().getProperties();
  timestamp_period_ = device_properties.limits.timestampPeriod;
  auto queue_properties =
      vk_physical_device().getQueueFamilyProperties()[vulkan_context_.queue_family_index];
  supports_timer_queries_ = queue_properties.timestampValidBits > 0;
}

Escher::~Escher() {
  shader_program_factory_->Clear();
  vk_device().waitIdle();
  Cleanup();

  // Everything that refers to a ResourceRecycler must be released before their
  // ResourceRecycler is.
  image_view_allocator_.reset();
  framebuffer_allocator_.reset();
  render_pass_cache_.reset();
  pipeline_layout_cache_.reset();
  mesh_manager_.reset();
  descriptor_set_allocator_cache_.reset();
  sampler_cache_.reset();

  // ResourceRecyclers must be released before the CommandBufferSequencer is,
  // since they register themselves with it.
  resource_recycler_.reset();
  buffer_cache_.reset();
}

bool Escher::Cleanup() {
  TRACE_DURATION("gfx", "Escher::Cleanup");
  bool finished = true;
  finished = command_buffer_pool()->Cleanup() && finished;
  if (auto pool = transfer_command_buffer_pool()) {
    finished = pool->Cleanup() && finished;
  }
  if (auto pool = protected_command_buffer_pool()) {
    finished = pool->Cleanup() && finished;
  }
  pipeline_builder_->MaybeStorePipelineCacheData();
  return finished;
}

void Escher::set_pipeline_builder(std::unique_ptr<PipelineBuilder> pipeline_builder) {
  pipeline_builder_ = std::move(pipeline_builder);
}

impl::CommandBufferPool* Escher::protected_command_buffer_pool() {
  if (allow_protected_memory() && !protected_command_buffer_pool_) {
    protected_command_buffer_pool_ =
        NewCommandBufferPool(vulkan_context_, command_buffer_sequencer_.get(), true);
  }
  return protected_command_buffer_pool_.get();
}

MeshBuilderPtr Escher::NewMeshBuilder(BatchGpuUploader* gpu_uploader, const MeshSpec& spec,
                                      size_t max_vertex_count, size_t max_index_count) {
  return mesh_manager()->NewMeshBuilder(gpu_uploader, spec, max_vertex_count, max_index_count);
}

ImagePtr Escher::NewRgbaImage(BatchGpuUploader* gpu_uploader, uint32_t width, uint32_t height,
                              uint8_t* bytes) {
  return image_utils::NewRgbaImage(image_cache(), gpu_uploader, width, height, bytes);
}

ImagePtr Escher::NewCheckerboardImage(BatchGpuUploader* gpu_uploader, uint32_t width,
                                      uint32_t height) {
  return image_utils::NewCheckerboardImage(image_cache(), gpu_uploader, width, height);
}

ImagePtr Escher::NewGradientImage(BatchGpuUploader* gpu_uploader, uint32_t width, uint32_t height) {
  return image_utils::NewGradientImage(image_cache(), gpu_uploader, width, height);
}

ImagePtr Escher::NewNoiseImage(BatchGpuUploader* gpu_uploader, uint32_t width, uint32_t height) {
  return image_utils::NewNoiseImage(image_cache(), gpu_uploader, width, height);
}

TexturePtr Escher::NewTexture(ImagePtr image, vk::Filter filter, vk::ImageAspectFlags aspect_mask,
                              bool use_unnormalized_coordinates) {
  TRACE_DURATION("gfx", "Escher::NewTexture (from image)");
  return Texture::New(resource_recycler(), std::move(image), filter, aspect_mask,
                      use_unnormalized_coordinates);
}

BufferPtr Escher::NewBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage_flags,
                            vk::MemoryPropertyFlags memory_property_flags) {
  TRACE_DURATION("gfx", "Escher::NewBuffer");
  return gpu_allocator()->AllocateBuffer(resource_recycler(), size, usage_flags,
                                         memory_property_flags);
}

TexturePtr Escher::NewTexture(vk::Format format, uint32_t width, uint32_t height,
                              uint32_t sample_count, vk::ImageUsageFlags usage_flags,
                              vk::Filter filter, vk::ImageAspectFlags aspect_flags,
                              bool use_unnormalized_coordinates,
                              vk::MemoryPropertyFlags memory_flags) {
  TRACE_DURATION("gfx", "Escher::NewTexture (new image)");
  ImageInfo image_info{.format = format,
                       .width = width,
                       .height = height,
                       .sample_count = sample_count,
                       .usage = usage_flags};
  image_info.memory_flags |= memory_flags;
  ImagePtr image = gpu_allocator()->AllocateImage(resource_recycler(), image_info);
  return Texture::New(resource_recycler(), std::move(image), filter, aspect_flags,
                      use_unnormalized_coordinates);
}

TexturePtr Escher::NewAttachmentTexture(vk::Format format, uint32_t width, uint32_t height,
                                        uint32_t sample_count, vk::Filter filter,
                                        vk::ImageUsageFlags usage_flags,
                                        bool is_transient_attachment, bool is_input_attachment,
                                        bool use_unnormalized_coordinates,
                                        vk::MemoryPropertyFlags memory_flags) {
  const auto pair = image_utils::IsDepthStencilFormat(format);
  usage_flags |= (pair.first || pair.second) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                                             : vk::ImageUsageFlagBits::eColorAttachment;
  if (is_transient_attachment) {
    // TODO(fxbug.dev/23860): when specifying that it is being used as a transient
    // attachment, we should use lazy memory if supported by the Vulkan
    // device... but only if no non-attachment flags are present.
    // TODO(fxbug.dev/23860): also, clients should probably just add this usage flag
    // themselves, rather than having a separate bool to do it.
    usage_flags |= vk::ImageUsageFlagBits::eTransientAttachment;
  }
  if (is_input_attachment) {
    usage_flags |= vk::ImageUsageFlagBits::eInputAttachment;
  }
  return NewTexture(format, width, height, sample_count, usage_flags, filter,
                    image_utils::FormatToColorOrDepthStencilAspectFlags(format),
                    use_unnormalized_coordinates, memory_flags);
}

ShaderProgramPtr Escher::GetProgramImpl(const std::string shader_paths[EnumCount<ShaderStage>()],
                                        ShaderVariantArgs args) {
  return shader_program_factory_->GetProgramImpl(shader_paths, std::move(args));
}

FramePtr Escher::NewFrame(const char* trace_literal, uint64_t frame_number, bool enable_gpu_logging,
                          escher::CommandBuffer::Type requested_type, bool use_protected_memory) {
  TRACE_DURATION("gfx", "escher::Escher::NewFrame ");

  // Check the type before cycling the framebuffer/descriptor-set allocators.
  // Without these checks it is possible to write into a Vulkan resource before
  // it is finished being used in a previous frame.
  // TODO(fxbug.dev/7194): The correct solution is not to use multiple Frames per frame.
  if (requested_type != CommandBuffer::Type::kTransfer) {
    // TODO(fxbug.dev/7288): Nothing calls Clear() on the DescriptorSetAllocators, so
    // their internal allocations are currently able to grow without bound.
    // DescriptorSets are not managed by ResourceRecyclers, so just
    // adding a call to Clear() here would be dangerous.
    descriptor_set_allocator_cache_->BeginFrame();
    pipeline_layout_cache_->BeginFrame();
  }
  if (requested_type == CommandBuffer::Type::kGraphics) {
    image_view_allocator_->BeginFrame();
    framebuffer_allocator_->BeginFrame();
  }

  return frame_manager_->NewFrame(trace_literal, frame_number, enable_gpu_logging, requested_type,
                                  use_protected_memory);
}

uint64_t Escher::GetNumGpuBytesAllocated() { return gpu_allocator()->GetTotalBytesAllocated(); }

}  // namespace escher
