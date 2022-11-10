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
class Sampler;
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

using BatchGpuUploaderPtr = fxl::RefPtr<BatchGpuUploader>;
using BufferPtr = fxl::RefPtr<Buffer>;
using CommandBufferPtr = fxl::RefPtr<CommandBuffer>;
using EscherWeakPtr = fxl::WeakPtr<Escher>;
using FramePtr = fxl::RefPtr<Frame>;
using FramebufferPtr = fxl::RefPtr<Framebuffer>;
using GpuMemPtr = fxl::RefPtr<GpuMem>;
using HackFilesystemPtr = fxl::RefPtr<HackFilesystem>;
using ImagePtr = fxl::RefPtr<Image>;
using ImageViewPtr = fxl::RefPtr<ImageView>;
using MaterialPtr = fxl::RefPtr<Material>;
using MeshPtr = fxl::RefPtr<Mesh>;
using MeshBuilderPtr = fxl::RefPtr<MeshBuilder>;
using PaperRendererPtr = fxl::RefPtr<PaperRenderer>;
// TODO(fxbug.dev/7174): move to vk/impl.  Cannot do this yet because there is already
// a PipelineLayout in impl/vk.
using PipelineLayoutPtr = fxl::RefPtr<PipelineLayout>;
using ResourcePtr = fxl::RefPtr<Resource>;
using RendererPtr = fxl::RefPtr<Renderer>;
using RenderPassPtr = fxl::RefPtr<RenderPass>;
using SamplerPtr = fxl::RefPtr<Sampler>;
using SemaphorePtr = fxl::RefPtr<Semaphore>;
using ShaderProgramPtr = fxl::RefPtr<ShaderProgram>;
using TexturePtr = fxl::RefPtr<Texture>;
using TimestampProfilerPtr = fxl::RefPtr<TimestampProfiler>;

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

using ModelDataPtr = fxl::RefPtr<ModelData>;
using PipelinePtr = fxl::RefPtr<Pipeline>;
using UniformBufferPoolWeakPtr = fxl::WeakPtr<UniformBufferPool>;

// From escher/vk/impl
class DescriptorSetAllocator;
class DescriptorSetAllocatorCache;
class Framebuffer;
class FramebufferAllocator;
class PipelineLayoutCache;
class RenderPass;
class RenderPassCache;

using FramebufferPtr = fxl::RefPtr<Framebuffer>;
using RenderPassPtr = fxl::RefPtr<RenderPass>;

// From escher/third_party/granite
struct DescriptorSetLayout;
struct ShaderModuleResourceLayout;

}  // namespace impl
}  // namespace escher

namespace shaderc {
class Compiler;
}

#endif  // SRC_UI_LIB_ESCHER_FORWARD_DECLARATIONS_H_
