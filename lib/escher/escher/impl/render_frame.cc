// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/render_frame.h"

#include "escher/impl/mesh_impl.h"
#include "escher/impl/model_uniform_writer.h"
#include "escher/impl/pipeline.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/shape/mesh.h"
#include "ftl/logging.h"

namespace escher {
namespace impl {

RenderFrame::RenderFrame(const VulkanContext& context,
                         vk::CommandBuffer command_buffer)
    : device_(context.device), command_buffer_(command_buffer) {}

RenderFrame::~RenderFrame() {
  FTL_DCHECK(!frame_started_ && !frame_ended_ && !fence_);
  // Owner is responsible for destroying command buffer.
}

void RenderFrame::BeginFrame(const FramebufferPtr& framebuffer,
                             const SemaphorePtr& frame_done,
                             FrameRetiredCallback frame_retired_callback,
                             uint64_t frame_number) {
  FTL_DCHECK(!frame_started_ && !frame_ended_);
  frame_started_ = true;

  frame_done_semaphore_ = frame_done;
  frame_retired_callback_ = frame_retired_callback;

  AddWaitSemaphore(framebuffer->TakeWaitSemaphore(),
                   vk::PipelineStageFlagBits::eColorAttachmentOutput);

  auto result = command_buffer_.begin(vk::CommandBufferBeginInfo());
  FTL_DCHECK(result == vk::Result::eSuccess);
}

void RenderFrame::EndFrameAndSubmit(vk::Queue queue) {
  FTL_DCHECK(frame_started_ && !frame_ended_ && !fence_);
  frame_ended_ = true;

  auto end_command_buffer_result = command_buffer_.end();
  FTL_DCHECK(end_command_buffer_result == vk::Result::eSuccess);

  vk::FenceCreateInfo fence_create_info;
  fence_ = ESCHER_CHECKED_VK_RESULT(device_.createFence(fence_create_info));

  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer_;
  submit_info.waitSemaphoreCount = wait_semaphores_for_submit_.size();
  submit_info.pWaitSemaphores = wait_semaphores_for_submit_.data();
  submit_info.pWaitDstStageMask = wait_semaphore_stages_.data();
  if (frame_done_semaphore_ && frame_done_semaphore_->value()) {
    vk::Semaphore sema = frame_done_semaphore_->value();
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &sema;
  } else {
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;
  }
  auto submit_result = queue.submit(1, &submit_info, fence_);
  if (submit_result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed to submit frame #" << frame_number_ << ": "
                     << to_string(submit_result);
    // Destroy the fence so that Retire() can make progress.
    // TODO: verify that destroying the fence here is the right thing to do.
    device_.destroyFence(fence_);
    fence_ = nullptr;  // So that Retire() can clear this frame from
  }
}

bool RenderFrame::Retire() {
  // TODO: clean up to put everything in consistent order with declarations.
  FTL_DCHECK(frame_started_ && frame_ended_);

  if (!fence_) {
    // Submission must have failed in EndFrameAndSubmit().
    // TODO: exiting early here means that we don't clear the
    // semaphores/resources/etc.
    FTL_LOG(ERROR) << "returning early without clearing semaphores/resources";
    return true;
  } else {
    // Check if fence has been reached.
    auto fence_status = device_.getFenceStatus(fence_);
    if (fence_status == vk::Result::eNotReady) {
      // Fence has not been reached; try again later.
      return false;
    }
  }

  device_.destroyFence(fence_);
  fence_ = nullptr;

  frame_number_ = 0;
  frame_started_ = false;
  frame_ended_ = false;

  used_resources_.clear();

  if (frame_retired_callback_) {
    frame_retired_callback_(frame_done_semaphore_);
  }
  frame_retired_callback_ = nullptr;
  frame_done_semaphore_ = nullptr;

  wait_semaphores_.clear();
  wait_semaphores_for_submit_.clear();
  wait_semaphore_stages_.clear();

  auto result = command_buffer_.reset(vk::CommandBufferResetFlags());
  FTL_DCHECK(result == vk::Result::eSuccess);

  return true;
}

void RenderFrame::DrawMesh(const MeshPtr& mesh) {
  AddUsedResource(mesh);

  AddWaitSemaphore(mesh->TakeWaitSemaphore(),
                   vk::PipelineStageFlagBits::eVertexInput);

  auto mesh_impl = static_cast<MeshImpl*>(mesh.get());
  vk::Buffer vbo = mesh_impl->vertex_buffer();
  vk::DeviceSize vbo_offset = mesh_impl->vertex_buffer_offset();
  uint32_t vbo_binding = mesh_impl->vertex_buffer_binding();
  command_buffer_.bindVertexBuffers(vbo_binding, 1, &vbo, &vbo_offset);
  command_buffer_.bindIndexBuffer(mesh_impl->index_buffer(),
                                  mesh_impl->vertex_buffer_offset(),
                                  vk::IndexType::eUint32);
  command_buffer_.drawIndexed(mesh_impl->num_indices, 1, 0, 0, 0);
}

void RenderFrame::AddWaitSemaphore(SemaphorePtr semaphore,
                                   vk::PipelineStageFlags stage) {
  if (semaphore) {
    // Retain semaphore to ensure that it doesn't prematurely die.
    wait_semaphores_.push_back(semaphore);
    // Build up lists that will be used when frame is submitted.
    wait_semaphores_for_submit_.push_back(semaphore->value());
    wait_semaphore_stages_.push_back(stage);
  }
}

void RenderFrame::AddUsedResource(ResourcePtr resource) {
  used_resources_.push_back(std::move(resource));
}

}  // namespace impl
}  // namespace escher
