// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace ftl {
template <typename T>
class RefPtr;
}  // namespace ftl

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

typedef ftl::RefPtr<Buffer> BufferPtr;
typedef ftl::RefPtr<Framebuffer> FramebufferPtr;
typedef ftl::RefPtr<GpuMem> GpuMemPtr;
typedef ftl::RefPtr<Image> ImagePtr;
typedef ftl::RefPtr<Material> MaterialPtr;
typedef ftl::RefPtr<Mesh> MeshPtr;
typedef ftl::RefPtr<MeshBuilder> MeshBuilderPtr;
typedef ftl::RefPtr<PaperRenderer> PaperRendererPtr;
typedef ftl::RefPtr<Resource> ResourcePtr;
typedef ftl::RefPtr<Renderer> RendererPtr;
typedef ftl::RefPtr<Semaphore> SemaphorePtr;
typedef ftl::RefPtr<Texture> TexturePtr;
typedef ftl::RefPtr<TimestampProfiler> TimestampProfilerPtr;

namespace impl {
class CommandBuffer;
class CommandBufferPool;
class CommandBufferSequencer;
class ComputeShader;
class EscherImpl;
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
class SsdoAccelerator;
class SsdoSampler;

typedef ftl::RefPtr<ModelDisplayList> ModelDisplayListPtr;
typedef ftl::RefPtr<Pipeline> PipelinePtr;

}  // namespace impl

}  // namespace escher
