// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace fxl {
template <typename T>
class RefPtr;
}  // namespace fxl

namespace escher {

class Buffer;
class Camera;
class Escher;
class Framebuffer;
class GpuAllocator;
class GpuMem;
class Image;
class ImageFactory;
class MeshBuilder;
class MeshBuilderFactory;
struct MeshSpec;
class Material;
class Mesh;
class Model;
class Object;
class PaperRenderer;
class Resource;
class ResourceRecycler;
class Renderer;
class Semaphore;
class Shape;
class Stage;
class Texture;
class Timestamper;
class TimestampProfiler;
class ViewingVolume;
struct VulkanContext;
struct VulkanSwapchain;

typedef fxl::RefPtr<Buffer> BufferPtr;
typedef fxl::RefPtr<Escher> EscherPtr;
typedef fxl::RefPtr<Framebuffer> FramebufferPtr;
typedef fxl::RefPtr<GpuMem> GpuMemPtr;
typedef fxl::RefPtr<Image> ImagePtr;
typedef fxl::RefPtr<Material> MaterialPtr;
typedef fxl::RefPtr<Mesh> MeshPtr;
typedef fxl::RefPtr<MeshBuilder> MeshBuilderPtr;
typedef fxl::RefPtr<PaperRenderer> PaperRendererPtr;
typedef fxl::RefPtr<Resource> ResourcePtr;
typedef fxl::RefPtr<Renderer> RendererPtr;
typedef fxl::RefPtr<Semaphore> SemaphorePtr;
typedef fxl::RefPtr<Texture> TexturePtr;
typedef fxl::RefPtr<TimestampProfiler> TimestampProfilerPtr;

namespace impl {
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
class Pipeline;
class PipelineCache;
class SsdoAccelerator;
class SsdoSampler;

typedef fxl::RefPtr<ModelData> ModelDataPtr;
typedef fxl::RefPtr<ModelDisplayList> ModelDisplayListPtr;
typedef fxl::RefPtr<ModelPipelineCache> ModelPipelineCachePtr;
typedef fxl::RefPtr<ModelRenderer> ModelRendererPtr;
typedef fxl::RefPtr<Pipeline> PipelinePtr;

}  // namespace impl

}  // namespace escher
