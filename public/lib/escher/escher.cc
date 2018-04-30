// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/escher.h"
#include "lib/escher/defaults/default_shader_program_factory.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/glsl_compiler.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/impl/vk/pipeline_cache.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/util/hasher.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/impl/descriptor_set_allocator.h"
#include "lib/escher/vk/impl/framebuffer_allocator.h"
#include "lib/escher/vk/impl/pipeline_layout_cache.h"
#include "lib/escher/vk/impl/render_pass_cache.h"
#include "lib/escher/vk/naive_gpu_allocator.h"
#include "lib/escher/vk/texture.h"
#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"

namespace escher {

namespace {

// Constructor helper.
std::unique_ptr<impl::CommandBufferPool> NewCommandBufferPool(
    const VulkanContext& context, impl::CommandBufferSequencer* sequencer) {
  return std::make_unique<impl::CommandBufferPool>(
      context.device, context.queue, context.queue_family_index, sequencer,
      true);
}

// Constructor helper.
std::unique_ptr<impl::CommandBufferPool> NewTransferCommandBufferPool(
    const VulkanContext& context, impl::CommandBufferSequencer* sequencer) {
  if (!context.transfer_queue) {
    return nullptr;
  } else {
    return std::make_unique<impl::CommandBufferPool>(
        context.device, context.transfer_queue,
        context.transfer_queue_family_index, sequencer, false);
  }
}

// Constructor helper.
std::unique_ptr<impl::GpuUploader> NewGpuUploader(
    EscherWeakPtr escher, impl::CommandBufferPool* main_pool,
    impl::CommandBufferPool* transfer_pool, GpuAllocator* allocator) {
  return std::make_unique<impl::GpuUploader>(
      std::move(escher), transfer_pool ? transfer_pool : main_pool, allocator);
}

// Constructor helper.
std::unique_ptr<impl::MeshManager> NewMeshManager(
    impl::CommandBufferPool* main_pool, impl::CommandBufferPool* transfer_pool,
    GpuAllocator* allocator, impl::GpuUploader* uploader,
    ResourceRecycler* resource_recycler) {
  return std::make_unique<impl::MeshManager>(
      transfer_pool ? transfer_pool : main_pool, allocator, uploader,
      resource_recycler);
}

}  // anonymous namespace

Escher::Escher(VulkanDeviceQueuesPtr device)
    : Escher(std::move(device), HackFilesystem::New()) {}

Escher::Escher(VulkanDeviceQueuesPtr device, HackFilesystemPtr filesystem)
    : renderer_count_(0),
      device_(std::move(device)),
      vulkan_context_(device_->GetVulkanContext()),
      gpu_allocator_(std::make_unique<NaiveGpuAllocator>(vulkan_context_)),
      command_buffer_sequencer_(
          std::make_unique<impl::CommandBufferSequencer>()),
      command_buffer_pool_(NewCommandBufferPool(
          vulkan_context_, command_buffer_sequencer_.get())),
      transfer_command_buffer_pool_(NewTransferCommandBufferPool(
          vulkan_context_, command_buffer_sequencer_.get())),
      glsl_compiler_(std::make_unique<impl::GlslToSpirvCompiler>()),
      shaderc_compiler_(std::make_unique<shaderc::Compiler>()),
      pipeline_cache_(std::make_unique<impl::PipelineCache>()),
      weak_factory_(this) {
  FXL_DCHECK(vulkan_context_.instance);
  FXL_DCHECK(vulkan_context_.physical_device);
  FXL_DCHECK(vulkan_context_.device);
  FXL_DCHECK(vulkan_context_.queue);
  // TODO: additional validation, e.g. ensure that queue supports both graphics
  // and compute.

  // Initialize instance variables that require |weak_factory_| to already have
  // been initialized.
  image_cache_ =
      std::make_unique<impl::ImageCache>(GetWeakPtr(), gpu_allocator());
  gpu_uploader_ =
      NewGpuUploader(GetWeakPtr(), command_buffer_pool(),
                     transfer_command_buffer_pool(), gpu_allocator());
  resource_recycler_ = std::make_unique<ResourceRecycler>(GetWeakPtr());
  mesh_manager_ =
      NewMeshManager(command_buffer_pool(), transfer_command_buffer_pool(),
                     gpu_allocator(), gpu_uploader(), resource_recycler());
  pipeline_layout_cache_ =
      std::make_unique<impl::PipelineLayoutCache>(resource_recycler()),
  render_pass_cache_ =
      std::make_unique<impl::RenderPassCache>(resource_recycler()),
  framebuffer_allocator_ = std::make_unique<impl::FramebufferAllocator>(
      resource_recycler(), render_pass_cache_.get());
  shader_program_factory_ = std::make_unique<DefaultShaderProgramFactory>(
      GetWeakPtr(), std::move(filesystem));

  // Query relevant Vulkan properties.
  auto device_properties = vk_physical_device().getProperties();
  timestamp_period_ = device_properties.limits.timestampPeriod;
  auto queue_properties =
      vk_physical_device()
          .getQueueFamilyProperties()[vulkan_context_.queue_family_index];
  supports_timer_queries_ = queue_properties.timestampValidBits > 0;
}

Escher::~Escher() {
  FXL_DCHECK(renderer_count_ == 0);
  shader_program_factory_->Clear();
  vk_device().waitIdle();
  Cleanup();

  // Everything that refers to a ResourceRecycler must be released before their
  // ResourceRecycler is.
  framebuffer_allocator_.reset();
  render_pass_cache_.reset();
  pipeline_layout_cache_.reset();
  mesh_manager_.reset();

  // ResourceRecyclers must be released before the CommandBufferSequencer is,
  // since they register themselves with it.
  resource_recycler_.reset();
  gpu_uploader_.reset();
}

bool Escher::Cleanup() {
  bool finished = true;
  finished = command_buffer_pool()->Cleanup() && finished;
  if (auto pool = transfer_command_buffer_pool()) {
    finished = pool->Cleanup() && finished;
  }
  return finished;
}

MeshBuilderPtr Escher::NewMeshBuilder(const MeshSpec& spec,
                                      size_t max_vertex_count,
                                      size_t max_index_count) {
  return mesh_manager()->NewMeshBuilder(spec, max_vertex_count,
                                        max_index_count);
}

ImagePtr Escher::NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes) {
  return image_utils::NewRgbaImage(image_cache(), gpu_uploader(), width, height,
                                   bytes);
}

ImagePtr Escher::NewCheckerboardImage(uint32_t width, uint32_t height) {
  return image_utils::NewCheckerboardImage(image_cache(), gpu_uploader(), width,
                                           height);
}

ImagePtr Escher::NewGradientImage(uint32_t width, uint32_t height) {
  return image_utils::NewGradientImage(image_cache(), gpu_uploader(), width,
                                       height);
}

ImagePtr Escher::NewNoiseImage(uint32_t width, uint32_t height) {
  return image_utils::NewNoiseImage(image_cache(), gpu_uploader(), width,
                                    height);
}

TexturePtr Escher::NewTexture(ImagePtr image, vk::Filter filter,
                              vk::ImageAspectFlags aspect_mask,
                              bool use_unnormalized_coordinates) {
  TRACE_DURATION("gfx", "Escher::NewTexture (from image)");
  return fxl::MakeRefCounted<Texture>(resource_recycler(), std::move(image),
                                      filter, aspect_mask,
                                      use_unnormalized_coordinates);
}

TexturePtr Escher::NewTexture(vk::Format format, uint32_t width,
                              uint32_t height, uint32_t sample_count,
                              vk::ImageUsageFlags usage_flags,
                              vk::Filter filter,
                              vk::ImageAspectFlags aspect_flags,
                              bool use_unnormalized_coordinates) {
  TRACE_DURATION("gfx", "Escher::NewTexture (new image)");
  ImageInfo image_info{.format = format,
                       .width = width,
                       .height = height,
                       .sample_count = sample_count,
                       .usage = usage_flags};
  ImagePtr image = Image::New(resource_recycler(), image_info, gpu_allocator());
  return fxl::MakeRefCounted<Texture>(resource_recycler(), std::move(image),
                                      filter, aspect_flags,
                                      use_unnormalized_coordinates);
}

TexturePtr Escher::NewAttachmentTexture(vk::Format format, uint32_t width,
                                        uint32_t height, uint32_t sample_count,
                                        vk::Filter filter,
                                        vk::ImageUsageFlags usage_flags,
                                        bool is_transient_attachment,
                                        bool is_input_attachment,
                                        bool use_unnormalized_coordinates) {
  const auto pair = image_utils::IsDepthStencilFormat(format);
  usage_flags |= (pair.first || pair.second)
                     ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                     : vk::ImageUsageFlagBits::eColorAttachment;
  if (is_transient_attachment) {
    usage_flags |= vk::ImageUsageFlagBits::eTransientAttachment;
  }
  if (is_input_attachment) {
    usage_flags |= vk::ImageUsageFlagBits::eInputAttachment;
  }
  return NewTexture(format, width, height, sample_count, usage_flags, filter,
                    image_utils::FormatToColorOrDepthStencilAspectFlags(format),
                    use_unnormalized_coordinates);
}

ShaderProgramPtr Escher::GetProgram(
    const std::string shader_paths[EnumCount<ShaderStage>()],
    ShaderVariantArgs args) {
  return shader_program_factory_->GetProgram(shader_paths, std::move(args));
}

FramePtr Escher::NewFrame(const char* trace_literal, uint64_t frame_number,
                          bool enable_gpu_logging) {
  TRACE_DURATION("gfx", "escher::Escher::NewFrame ");
  for (auto& pair : descriptor_set_allocators_) {
    pair.second->BeginFrame();
  }
  framebuffer_allocator_->BeginFrame();

  auto frame = fxl::AdoptRef<Frame>(
      new Frame(this, frame_number, trace_literal, enable_gpu_logging));
  frame->BeginFrame();
  return frame;
}

uint64_t Escher::GetNumGpuBytesAllocated() {
  return gpu_allocator()->total_slab_bytes();
}

impl::DescriptorSetAllocator* Escher::GetDescriptorSetAllocator(
    const impl::DescriptorSetLayout& layout) {
  TRACE_DURATION("gfx", "escher::Escher::GetDescriptorSetAllocator");
  static_assert(sizeof(impl::DescriptorSetLayout) == 32,
                "hash code below must be updated");
  Hasher h;
  h.u32(layout.sampled_image_mask);
  h.u32(layout.storage_image_mask);
  h.u32(layout.uniform_buffer_mask);
  h.u32(layout.storage_buffer_mask);
  h.u32(layout.sampled_buffer_mask);
  h.u32(layout.input_attachment_mask);
  h.u32(layout.fp_mask);
  h.u32(static_cast<uint32_t>(layout.stages));
  Hash hash = h.value();

  auto it = descriptor_set_allocators_.find(hash);
  if (it != descriptor_set_allocators_.end()) {
    FXL_DCHECK(layout == it->second->layout()) << "hash collision.";
    return it->second.get();
  }

  TRACE_DURATION("gfx", "escher::Escher::GetDescriptorSetAllocator[creation]");
  auto new_allocator = new impl::DescriptorSetAllocator(vk_device(), layout);
  descriptor_set_allocators_.emplace_hint(it, hash, new_allocator);
  return new_allocator;
}

}  // namespace escher
