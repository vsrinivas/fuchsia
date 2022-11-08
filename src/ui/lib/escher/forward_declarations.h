// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FORWARD_DECLARATIONS_H_
#define SRC_UI_LIB_ESCHER_FORWARD_DECLARATIONS_H_

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace escher {

class BatchGpuDownloader;
class BatchGpuUploader;
class BlockAllocator;
class Buffer;
class BufferCache;
class Camera;
class ChainedSemaphoreGenerator;
class CommandBuffer;
class DebugFont;
class DefaultShaderProgramFactory;
class Escher;
class Frame;
class Framebuffer;
class GpuAllocator;
class GpuMem;
class HackFilesystem;
class Image;
class ImageFactory;
class ImageLayoutUpdater;
class ImageView;
class MeshBuilder;
class MeshBuilderFactory;
struct MeshSpec;
class Material;
class Mesh;
class Model;
class Object;
class PipelineBuilder;
// TODO(fxbug.dev/7174): move to vk/impl.  Cannot do this yet because there is already
// a PipelineLayout in impl/vk.
class PipelineLayout;
class PaperRenderer;
class PaperRenderQueue;
class PaperShapeCache;
class Resource;
class ResourceRecycler;
class Renderer;
class RenderPass;
struct RenderPassInfo;
struct RenderQueueItem;
class SamplerCache;
class Semaphore;
class ShaderProgram;
class Shape;
class Stage;
class Texture;
class TimestampProfiler;
struct UniformAllocation;
class ViewingVolume;
struct VulkanContext;
struct VulkanSwapchain;

typedef fxl::RefPtr<BatchGpuUploader> BatchGpuUploaderPtr;
typedef fxl::RefPtr<Buffer> BufferPtr;
typedef fxl::RefPtr<CommandBuffer> CommandBufferPtr;
typedef fxl::WeakPtr<Escher> EscherWeakPtr;
typedef fxl::RefPtr<Frame> FramePtr;
typedef fxl::RefPtr<Framebuffer> FramebufferPtr;
typedef fxl::RefPtr<GpuMem> GpuMemPtr;
typedef fxl::RefPtr<HackFilesystem> HackFilesystemPtr;
typedef fxl::RefPtr<Image> ImagePtr;
typedef fxl::RefPtr<ImageView> ImageViewPtr;
typedef fxl::RefPtr<Material> MaterialPtr;
typedef fxl::RefPtr<Mesh> MeshPtr;
typedef fxl::RefPtr<MeshBuilder> MeshBuilderPtr;
typedef fxl::RefPtr<PaperRenderer> PaperRendererPtr;
// TODO(fxbug.dev/7174): move to vk/impl.  Cannot do this yet because there is already
// a PipelineLayout in impl/vk.
typedef fxl::RefPtr<PipelineLayout> PipelineLayoutPtr;
typedef fxl::RefPtr<Resource> ResourcePtr;
typedef fxl::RefPtr<Renderer> RendererPtr;
typedef fxl::RefPtr<RenderPass> RenderPassPtr;
typedef fxl::RefPtr<Semaphore> SemaphorePtr;
typedef fxl::RefPtr<ShaderProgram> ShaderProgramPtr;
typedef fxl::RefPtr<Texture> TexturePtr;
typedef fxl::RefPtr<TimestampProfiler> TimestampProfilerPtr;

namespace hmd {
class PoseBufferLatchingShader;
}  // namespace hmd

namespace impl {
// From deprecated escher/impl directory.
class CommandBuffer;
class CommandBufferPool;
class CommandBufferSequencer;
class ComputeShader;
class FrameManager;
class ImageCache;
class MeshManager;
class MeshShaderBinding;
class ModelData;
class Pipeline;
class UniformBufferPool;

typedef fxl::RefPtr<ModelData> ModelDataPtr;
typedef fxl::RefPtr<Pipeline> PipelinePtr;
typedef fxl::WeakPtr<UniformBufferPool> UniformBufferPoolWeakPtr;

// From escher/vk/impl
class DescriptorSetAllocator;
class DescriptorSetAllocatorCache;
class Framebuffer;
class FramebufferAllocator;
class PipelineLayoutCache;
class RenderPass;
class RenderPassCache;

typedef fxl::RefPtr<Framebuffer> FramebufferPtr;
typedef fxl::RefPtr<RenderPass> RenderPassPtr;

// From escher/third_party/granite
struct DescriptorSetLayout;
struct ShaderModuleResourceLayout;

}  // namespace impl
}  // namespace escher

namespace shaderc {
class Compiler;
}

#endif  // SRC_UI_LIB_ESCHER_FORWARD_DECLARATIONS_H_
