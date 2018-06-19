// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_FORWARD_DECLARATIONS_H_
#define LIB_ESCHER_FORWARD_DECLARATIONS_H_

#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace escher {

class Buffer;
class Camera;
class CommandBuffer;
class DefaultShaderProgramFactory;
class Escher;
class Frame;
class Framebuffer;
class GpuAllocator;
class GpuMem;
class HackFilesystem;
class Image;
class ImageFactory;
class ImageView;
class MeshBuilder;
class MeshBuilderFactory;
struct MeshSpec;
class Material;
class Mesh;
class Model;
class Object;
// TODO(ES-83): move to vk/impl.  Cannot do this yet because there is already
// a PipelineLayout in impl/vk.
class PipelineLayout;
class PaperRenderer;
class Resource;
class ResourceRecycler;
class Renderer;
class RenderPass;
struct RenderPassInfo;
class Semaphore;
class ShaderProgram;
class ShadowMap;
class ShadowMapRenderer;
class Shape;
class Stage;
class Texture;
class Timestamper;
class TimestampProfiler;
class ViewingVolume;
struct VulkanContext;
struct VulkanSwapchain;

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
// TODO(ES-83): move to vk/impl.  Cannot do this yet because there is already
// a PipelineLayout in impl/vk.
typedef fxl::RefPtr<PipelineLayout> PipelineLayoutPtr;
typedef fxl::RefPtr<Resource> ResourcePtr;
typedef fxl::RefPtr<Renderer> RendererPtr;
typedef fxl::RefPtr<RenderPass> RenderPassPtr;
typedef fxl::RefPtr<Semaphore> SemaphorePtr;
typedef fxl::RefPtr<ShaderProgram> ShaderProgramPtr;
typedef fxl::RefPtr<ShadowMap> ShadowMapPtr;
typedef fxl::RefPtr<ShadowMapRenderer> ShadowMapRendererPtr;
typedef fxl::RefPtr<Texture> TexturePtr;
typedef fxl::RefPtr<TimestampProfiler> TimestampProfilerPtr;

namespace impl {
// From deprecated escher/impl directory.
class CommandBuffer;
class CommandBufferPool;
class CommandBufferSequencer;
class ComputeShader;
class GlslToSpirvCompiler;
class GpuUploader;
class ImageCache;
class MeshManager;
class MeshShaderBinding;
class ModelData;
class ModelDisplayList;
class ModelPipeline;
class ModelPipelineCache;
class ModelRenderer;
class ModelRenderPass;
class Pipeline;
class PipelineCache;
class SsdoAccelerator;
class SsdoSampler;

typedef fxl::RefPtr<ModelData> ModelDataPtr;
typedef fxl::RefPtr<ModelDisplayList> ModelDisplayListPtr;
typedef fxl::RefPtr<ModelPipelineCache> ModelPipelineCachePtr;
typedef fxl::RefPtr<ModelRenderer> ModelRendererPtr;
typedef fxl::RefPtr<ModelRenderPass> ModelRenderPassPtr;
typedef fxl::RefPtr<Pipeline> PipelinePtr;

// From escher/vk/impl
class DescriptorSetAllocator;
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

#endif  // LIB_ESCHER_FORWARD_DECLARATIONS_H_
