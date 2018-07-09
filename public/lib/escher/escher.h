// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_ESCHER_H_
#define LIB_ESCHER_ESCHER_H_

#include <memory>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/shape/mesh_builder_factory.h"
#include "lib/escher/status.h"
#include "lib/escher/util/hash.h"
#include "lib/escher/util/hash_map.h"
#include "lib/escher/vk/shader_program_factory.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/fxl/macros.h"

namespace escher {

// Escher is the primary class used by clients of the Escher library.
//
// Escher is currently not thread-safe; it (and all objects obtained from it)
// must be used from a single thread.
class Escher : public MeshBuilderFactory, public ShaderProgramFactory {
 public:
  // Escher does not take ownership of the objects in the Vulkan context.  It is
  // up to the application to eventually destroy them, and also to ensure that
  // they outlive the Escher instance.
  explicit Escher(VulkanDeviceQueuesPtr device);
  Escher(VulkanDeviceQueuesPtr device, HackFilesystemPtr filesystem);
  ~Escher();

  EscherWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Implement MeshBuilderFactory interface.
  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec, size_t max_vertex_count,
                                size_t max_index_count) override;

  // Return new Image containing the provided pixels.
  ImagePtr NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes);
  // Returns RGBA image.
  ImagePtr NewCheckerboardImage(uint32_t width, uint32_t height);
  // Returns RGBA image.
  ImagePtr NewGradientImage(uint32_t width, uint32_t height);
  // Returns single-channel luminance image.
  ImagePtr NewNoiseImage(uint32_t width, uint32_t height);
  // Return a new Frame, which is passed to Renderers to obtain and submit
  // command buffers, to add timestamps for GPU profiling, etc.  If
  // |enable_gpu_logging| is true, GPU profiling timestamps will be logged via
  // FXL_LOG().
  FramePtr NewFrame(const char* trace_literal, uint64_t frame_number,
                    bool enable_gpu_logging = false);
  // Return the number of frames that have been returned by NewFrame() and which
  // have not finished rendering (after having EndFrame() invoked upon them).
  uint32_t GetNumOutstandingFrames() const;

  // Construct a new Texture, which encapsulates a newly-created VkImageView and
  // VkSampler.  |aspect_mask| is used to create the VkImageView, and |filter|
  // and |use_unnormalized_coordinates| are used to create the VkSampler.
  TexturePtr NewTexture(
      ImagePtr image, vk::Filter filter,
      vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor,
      bool use_unnormalized_coordinates = false);

  // Construct a new Texture, which encapsulates a newly-created VkImage,
  // VkImageView and VkSampler.  |aspect_mask| is used to create the
  // VkImageView, and |filter| and |use_unnormalized_coordinates| are used to
  // create the VkSampler.
  TexturePtr NewTexture(vk::Format format, uint32_t width, uint32_t height,
                        uint32_t sample_count, vk::ImageUsageFlags usage_flags,
                        vk::Filter filter, vk::ImageAspectFlags aspect_flags,
                        bool use_unnormalized_coordinates = false);

  // Same as the NewTexture() variant that creates the image, except that it
  // automatically sets up the vk::ImageAspectFlags, and adds the following to
  // |additional_usage_flags|:
  //   - either eColorAttachment or eDepthAttachment, depending on |format|
  //   - optionally eTransientAttachment, depending on |is_transient|
  //   - optionally eInputAttachment, depending on |is_input|
  TexturePtr NewAttachmentTexture(
      vk::Format format, uint32_t width, uint32_t height, uint32_t sample_count,
      vk::Filter filter,
      vk::ImageUsageFlags additional_usage_flags = vk::ImageUsageFlags(),
      bool is_transient_attachment = false, bool is_input_attachment = false,
      bool use_unnormalized_coordinates = false);

  uint64_t GetNumGpuBytesAllocated();

  impl::DescriptorSetAllocator* GetDescriptorSetAllocator(
      const impl::DescriptorSetLayout& layout);

  // Do periodic housekeeping.  This is called by Renderer::EndFrame(), so you
  // don't need to call it if your application is constantly renderering.
  // However, if your app enters a "quiet period" then you might want to
  // arrange to call Cleanup() after the last frame has finished rendering.
  // Return true if cleanup was complete, and false if more cleanup remains
  // (in that case, the app should wait a moment before calling Cleanup()
  // again).
  bool Cleanup();

  VulkanDeviceQueues* device() const { return device_.get(); }
  vk::Device vk_device() const { return device_->vk_device(); }
  vk::PhysicalDevice vk_physical_device() const {
    return device_->vk_physical_device();
  }
  const VulkanContext& vulkan_context() const { return vulkan_context_; }

  ResourceRecycler* resource_recycler() { return resource_recycler_.get(); }
  GpuAllocator* gpu_allocator() { return gpu_allocator_.get(); }
  impl::GpuUploader* gpu_uploader() { return gpu_uploader_.get(); }
  impl::CommandBufferSequencer* command_buffer_sequencer() {
    return command_buffer_sequencer_.get();
  }
  impl::GlslToSpirvCompiler* glsl_compiler() { return glsl_compiler_.get(); }
  shaderc::Compiler* shaderc_compiler() { return shaderc_compiler_.get(); }
  impl::ImageCache* image_cache() { return image_cache_.get(); }
  impl::MeshManager* mesh_manager() { return mesh_manager_.get(); }
  impl::PipelineLayoutCache* pipeline_layout_cache() {
    return pipeline_layout_cache_.get();
  }
  impl::RenderPassCache* render_pass_cache() const {
    return render_pass_cache_.get();
  }
  impl::FramebufferAllocator* framebuffer_allocator() const {
    return framebuffer_allocator_.get();
  }

  // Pool for CommandBuffers submitted on the main queue.
  impl::CommandBufferPool* command_buffer_pool() {
    return command_buffer_pool_.get();
  }
  // Pool for CommandBuffers submitted on the transfer queue (if one exists).
  impl::CommandBufferPool* transfer_command_buffer_pool() {
    return transfer_command_buffer_pool_.get();
  }

  DefaultShaderProgramFactory* shader_program_factory() {
    return shader_program_factory_.get();
  }

  // Check if GPU performance profiling is supported.
  bool supports_timer_queries() const { return supports_timer_queries_; }
  float timestamp_period() const { return timestamp_period_; }

 private:
  // Called by Renderer constructor and destructor, respectively.
  friend class Renderer;
  void IncrementRendererCount() { ++renderer_count_; }
  void DecrementRendererCount() { --renderer_count_; }
  std::atomic<uint32_t> renderer_count_;

  // |ShaderProgramFactory|
  ShaderProgramPtr GetProgram(
      const std::string shader_paths[EnumCount<ShaderStage>()],
      ShaderVariantArgs args) override;

  VulkanDeviceQueuesPtr device_;
  VulkanContext vulkan_context_;

  // These can be constructed without an EscherWeakPtr.
  std::unique_ptr<GpuAllocator> gpu_allocator_;
  std::unique_ptr<impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::unique_ptr<impl::CommandBufferPool> command_buffer_pool_;
  std::unique_ptr<impl::CommandBufferPool> transfer_command_buffer_pool_;
  std::unique_ptr<impl::GlslToSpirvCompiler> glsl_compiler_;
  std::unique_ptr<shaderc::Compiler> shaderc_compiler_;
  std::unique_ptr<impl::PipelineCache> pipeline_cache_;
  // Everything below this point requires |weak_factory_| to be initialized
  // before they can be constructed.

  std::unique_ptr<impl::ImageCache> image_cache_;
  std::unique_ptr<impl::GpuUploader> gpu_uploader_;
  std::unique_ptr<ResourceRecycler> resource_recycler_;
  std::unique_ptr<impl::MeshManager> mesh_manager_;
  std::unique_ptr<DefaultShaderProgramFactory> shader_program_factory_;

  std::unique_ptr<impl::PipelineLayoutCache> pipeline_layout_cache_;

  std::unique_ptr<impl::RenderPassCache> render_pass_cache_;
  std::unique_ptr<impl::FramebufferAllocator> framebuffer_allocator_;
  std::unique_ptr<impl::FrameManager> frame_manager_;

  HashMap<Hash, std::unique_ptr<impl::DescriptorSetAllocator>>
      descriptor_set_allocators_;

  bool supports_timer_queries_ = false;
  float timestamp_period_ = 0.f;

  fxl::WeakPtrFactory<Escher> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(Escher);
};

}  // namespace escher

#endif  // LIB_ESCHER_ESCHER_H_
