// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace ftl {
template <typename T>
class RefPtr;
}  // namespace ftl

namespace escher {

class Escher;
class Framebuffer;
class Image;
class MeshBuilder;
struct MeshSpec;
class Material;
class Mesh;
class Model;
class PaperRenderer;
class Renderer;
class Semaphore;
class Shape;
class Stage;
struct VulkanContext;
struct VulkanSwapchain;

typedef ftl::RefPtr<Framebuffer> FramebufferPtr;
typedef ftl::RefPtr<Image> ImagePtr;
typedef ftl::RefPtr<Mesh> MeshPtr;
typedef ftl::RefPtr<MeshBuilder> MeshBuilderPtr;
typedef ftl::RefPtr<PaperRenderer> PaperRendererPtr;
typedef ftl::RefPtr<Renderer> RendererPtr;
typedef ftl::RefPtr<Semaphore> SemaphorePtr;

namespace impl {
class EscherImpl;
class ImageCache;
class MeshImpl;
class MeshManager;
class ModelData;
class ModelRenderer;
class PipelineCache;
class RenderFrame;
class Resource;

typedef ftl::RefPtr<Resource> ResourcePtr;
}  // namespace impl

}  // namespace escher
