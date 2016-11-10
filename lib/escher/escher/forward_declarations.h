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
class Escher;
class Framebuffer;
class Image;
class MeshBuilder;
class MeshBuilderFactory;
struct MeshSpec;
class Material;
class Mesh;
class Model;
class Object;
class PaperRenderer;
class Renderer;
class Semaphore;
class Shape;
class Stage;
class Texture;
struct VulkanContext;
struct VulkanSwapchain;

typedef ftl::RefPtr<Buffer> BufferPtr;
typedef ftl::RefPtr<Framebuffer> FramebufferPtr;
typedef ftl::RefPtr<Image> ImagePtr;
typedef ftl::RefPtr<Material> MaterialPtr;
typedef ftl::RefPtr<Mesh> MeshPtr;
typedef ftl::RefPtr<MeshBuilder> MeshBuilderPtr;
typedef ftl::RefPtr<PaperRenderer> PaperRendererPtr;
typedef ftl::RefPtr<Renderer> RendererPtr;
typedef ftl::RefPtr<Semaphore> SemaphorePtr;
typedef ftl::RefPtr<Texture> TexturePtr;

namespace impl {
class CommandBuffer;
class CommandBufferPool;
class EscherImpl;
class GpuAllocator;
class GpuMem;
class ImageCache;
class MeshImpl;
class MeshManager;
struct MeshSpecImpl;
class ModelData;
class ModelPipeline;
class ModelPipelineCache;
class ModelRenderer;
class Resource;
class SsdoSampler;

typedef ftl::RefPtr<GpuMem> GpuMemPtr;
typedef ftl::RefPtr<Resource> ResourcePtr;

}  // namespace impl

}  // namespace escher
