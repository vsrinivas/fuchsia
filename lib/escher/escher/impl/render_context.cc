// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/render_context.h"
#include "escher/impl/swapchain_manager.h"
#include "escher/impl/temp_frame_renderer.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/util/cplusplus.h"

namespace escher {
namespace impl {

RenderContext::RenderContext(const VulkanContext& context)
    : context_(context) {}

vk::Result RenderContext::Initialize(const VulkanSwapchain& swapchain) {
  vk::Result result = vk::Result::eSuccess;

  depth_format_ = ESCHER_CHECKED_VK_RESULT(
      GetSupportedDepthFormat(context_.physical_device));

  result = CreateRenderPass();
  if (result != vk::Result::eSuccess)
    return result;

  result = CreateCommandPool();
  if (result != vk::Result::eSuccess)
    return result;

  image_cache_ =
      make_unique<ImageCache>(context_.device, context_.physical_device);

  swapchain_manager_ = make_unique<SwapchainManager>(
      context_, render_pass_, image_cache_.get(), depth_format_);
  result = swapchain_manager_->Init();
  if (result != vk::Result::eSuccess)
    return result;
  swapchain_manager_->SetSwapchain(swapchain);

  // Set up TempFrameRenderer, just so that we can get something up on the
  // screen.
  FTL_CHECK(!temp_frame_renderer_);
  temp_frame_renderer_ = make_unique<TempFrameRenderer>(context_, render_pass_);

  return vk::Result::eSuccess;
}

RenderContext::~RenderContext() {
  CleanupFinishedFrames();
  FTL_DCHECK(pending_frames_.empty());

  DestroyCommandPool();
  temp_frame_renderer_.reset();
  swapchain_manager_.reset();
  image_cache_.reset();
  if (render_pass_)
    context_.device.destroyRenderPass(render_pass_);
}

vk::Result RenderContext::Render(const Stage& stage, const Model& model) {
  CleanupFinishedFrames();

  // Acquire an image to render into.
  auto frame_info_result = swapchain_manager_->BeginFrame();
  if (frame_info_result.result != vk::Result::eSuccess)
    return frame_info_result.result;
  SwapchainManager::FrameInfo frame_info = frame_info_result.value;

  // By calling frame.Render(), we generate all command buffers required to
  // render this frame.
  Frame frame(this, frame_number_++);
  vk::Result result = frame.Render(stage, model, frame_info.framebuffer);
  if (result != vk::Result::eSuccess) {
    CleanupFrame(&frame);
    return result;
  }

  // Create a fence so we know when the frame is finished.
  vk::Fence fence;
  {
    vk::FenceCreateInfo info;
    auto result = context_.device.createFence(info);
    if (result.result != vk::Result::eSuccess) {
      FTL_LOG(WARNING) << "failed to create Fence: "
                       << to_string(result.result);
      CleanupFrame(&frame);
      return result.result;
    }
    fence = result.value;
  }

  // Submit the frame.
  {
    vk::SubmitInfo info;
    info.commandBufferCount = frame.buffers_.size();
    info.pCommandBuffers = frame.buffers_.data();
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &frame_info.image_available_semaphore;
    vk::PipelineStageFlags flags =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    info.pWaitDstStageMask = &flags;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &frame_info.render_finished_semaphore;
    auto result = context_.queue.submit(1, &info, fence);
    if (result != vk::Result::eSuccess) {
      FTL_LOG(WARNING) << "failed to submit CommandBuffers for frame #"
                       << frame.frame_number_ << ": " << to_string(result);
      CleanupFrame(&frame);
      context_.device.destroyFence(fence);
      return result;
    }

    frame.fence_ = std::move(fence);
    pending_frames_.push(std::move(frame));
  }

  // TODO: not clear how to handle errors here... what frame cleanup is
  // appropriate?
  swapchain_manager_->EndFrame();

  return vk::Result::eSuccess;
}

void RenderContext::SetSwapchain(const VulkanSwapchain& swapchain) {
  if (swapchain_manager_)
    swapchain_manager_->SetSwapchain(swapchain);
}

void RenderContext::CleanupFinishedFrames() {
  while (!pending_frames_.empty()) {
    auto& frame = pending_frames_.front();
    if (vk::Result::eNotReady == context_.device.getFenceStatus(frame.fence_)) {
      // The first frame in the queue is not finished, so neither are the rest.
      return;
    }
    CleanupFrame(&frame);
    pending_frames_.pop();
  }
}

void RenderContext::CleanupFrame(Frame* frame) {
  FTL_DCHECK(frame);
  FTL_DCHECK(context_.device);

  if (frame->fence_) {
    FTL_DCHECK(vk::Result::eNotReady !=
               context_.device.getFenceStatus(frame->fence_));
    context_.device.destroyFence(frame->fence_);
    frame->fence_ = nullptr;
  }
  context_.device.freeCommandBuffers(command_pool_, frame->buffers_);
  frame->buffers_.clear();
}

vk::Result RenderContext::CreateRenderPass() {
  FTL_DCHECK(context_.device);
  FTL_DCHECK(!render_pass_);

  std::vector<vk::AttachmentDescription> attachments(2);
  auto& color_attachment = attachments[0];
  auto& depth_attachment = attachments[1];

  // TODO: VulkanProvider should know the swapchain format and we should use it.
  color_attachment.format = vk::Format::eB8G8R8A8Unorm;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment.initialLayout = vk::ImageLayout::eUndefined;
  color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

  // TODO: create and use a depth attachment image.
  depth_attachment.format = ESCHER_CHECKED_VK_RESULT(
      GetSupportedDepthFormat(context_.physical_device));
  depth_attachment.samples = vk::SampleCountFlagBits::e1;
  depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
  depth_attachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  vk::AttachmentReference color_reference;
  color_reference.attachment = 0;
  color_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference depth_reference;
  depth_reference.attachment = 1;
  depth_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // We need at least one subpass.
  vk::SubpassDescription subpass;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.pDepthStencilAttachment = &depth_reference;
  subpass.inputAttachmentCount = 0;  // no other subpasses to sample from

  // Even though we have a single subpass, we need to declare dependencies to
  // support the layout transitions specified by the attachment references.
  std::vector<vk::SubpassDependency> dependencies(2);

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp ?!?
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  dependencies[0].dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                  vk::AccessFlagBits::eColorAttachmentWrite;
  dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  dependencies[1].srcSubpass = 0;  // our sole subpass
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                  vk::AccessFlagBits::eColorAttachmentWrite;
  dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // Create the render pass, now that we can fully specify it.
  vk::RenderPassCreateInfo info;
  info.attachmentCount = static_cast<uint32_t>(attachments.size());
  info.pAttachments = attachments.data();
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = static_cast<uint32_t>(dependencies.size());
  info.pDependencies = dependencies.data();

  render_pass_ =
      ESCHER_CHECKED_VK_RESULT(context_.device.createRenderPass(info));
  return vk::Result::eSuccess;
}

vk::Result RenderContext::CreateCommandPool() {
  FTL_DCHECK(context_.device);
  FTL_DCHECK(!command_pool_);

  vk::CommandPoolCreateInfo info;
  info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  info.queueFamilyIndex = context_.queue_family_index;
  auto result = context_.device.createCommandPool(info);
  if (result.result == vk::Result::eSuccess) {
    command_pool_ = result.value;
  } else {
    ESCHER_LOG_VK_ERROR(result.result, "failed to create CommandPool");
  }
  return result.result;
}

void RenderContext::DestroyCommandPool() {
  FTL_DCHECK(pending_frames_.empty());

  if (command_pool_) {
    context_.device.destroyCommandPool(command_pool_);
    command_pool_ = nullptr;
  }
}

RenderContext::Frame::Frame(Frame&& other)
    : render_context_(other.render_context_),
      vulkan_context_(other.vulkan_context_),
      fence_(other.fence_),
      buffers_(std::move(other.buffers_)),
      frame_number_(other.frame_number_) {
  other.render_context_ = nullptr;
  other.fence_ = nullptr;
  other.frame_number_ = UINT64_MAX;
}

RenderContext::Frame::Frame(RenderContext* context, uint64_t frame_number)
    : render_context_(context),
      vulkan_context_(render_context_->context_),
      frame_number_(frame_number) {}

vk::Result RenderContext::Frame::Render(const Stage& stage,
                                        const Model& model,
                                        vk::Framebuffer framebuffer) {
  return render_context_->temp_frame_renderer_->Render(this, framebuffer);
}

AllocateCommandBuffersResult RenderContext::Frame::AllocateCommandBuffers(
    uint32_t count,
    vk::CommandBufferLevel level) {
  // Create the buffers.
  vk::CommandBufferAllocateInfo info;
  info.commandPool = render_context_->command_pool_;
  info.level = level;
  info.commandBufferCount = count;
  auto result = vulkan_context_.device.allocateCommandBuffers(info);
  if (result.result == vk::Result::eSuccess) {
    // Remember the buffers so they can be freed when the frame is finished.
    buffers_.insert(buffers_.end(), result.value.begin(), result.value.end());

    // Start one-time-use recording.
    // TODO: we will need a way to continue renderpasses.
    for (auto& buffer : result.value) {
      vk::CommandBufferBeginInfo info;
      info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
      vk::Result result = buffer.begin(info);
      // The only way that this should be able to fail is out of host/device
      // memory; not sure how we should handle those cases.
      FTL_CHECK(result == vk::Result::eSuccess);
    }
  }
  return result;
}

}  // namespace impl
}  // namespace escher
