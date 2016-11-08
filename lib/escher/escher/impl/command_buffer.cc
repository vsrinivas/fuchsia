// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/command_buffer.h"

#include "escher/impl/mesh_impl.h"
#include "escher/impl/resource.h"

#include "ftl/macros.h"

namespace escher {
namespace impl {

CommandBuffer::CommandBuffer(vk::Device device,
                             vk::CommandBuffer command_buffer,
                             vk::Fence fence)
    : device_(device), command_buffer_(command_buffer), fence_(fence) {}

CommandBuffer::~CommandBuffer() {
  FTL_DCHECK(!is_active_ && !is_submitted_);
  // Owner is responsible for destroying command buffer and fence.
}

void CommandBuffer::Begin(CommandBufferFinishedCallback callback) {
  FTL_DCHECK(!is_active_ && !is_submitted_);
  is_active_ = true;
  callback_ = std::move(callback);
  auto result = command_buffer_.begin(vk::CommandBufferBeginInfo());
  FTL_DCHECK(result == vk::Result::eSuccess);
}

bool CommandBuffer::Submit(vk::Queue queue) {
  FTL_DCHECK(is_active_ && !is_submitted_);
  is_submitted_ = true;

  auto end_command_buffer_result = command_buffer_.end();
  FTL_DCHECK(end_command_buffer_result == vk::Result::eSuccess);

  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer_;
  submit_info.waitSemaphoreCount = wait_semaphores_for_submit_.size();
  submit_info.pWaitSemaphores = wait_semaphores_for_submit_.data();
  submit_info.pWaitDstStageMask = wait_semaphore_stages_.data();
  submit_info.signalSemaphoreCount = signal_semaphores_for_submit_.size();
  submit_info.pSignalSemaphores = signal_semaphores_for_submit_.data();

  auto submit_result = queue.submit(1, &submit_info, fence_);
  if (submit_result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed queue submission: " << to_string(submit_result);
    // Clearing these flags allows Retire() to make progress.
    is_active_ = is_submitted_ = false;
    return false;
  }
  return true;
}

void CommandBuffer::AddWaitSemaphore(SemaphorePtr semaphore,
                                     vk::PipelineStageFlags stage) {
  if (semaphore) {
    // Build up list that will be used when frame is submitted.
    wait_semaphores_for_submit_.push_back(semaphore->value());
    wait_semaphore_stages_.push_back(stage);
    // Retain semaphore to ensure that it doesn't prematurely die.
    wait_semaphores_.push_back(std::move(semaphore));
  }
}

void CommandBuffer::AddSignalSemaphore(SemaphorePtr semaphore) {
  if (semaphore) {
    // Build up list that will be used when frame is submitted.
    signal_semaphores_for_submit_.push_back(semaphore->value());
    // Retain semaphore to ensure that it doesn't prematurely die.
    signal_semaphores_.push_back(std::move(semaphore));
  }
}

void CommandBuffer::AddUsedResource(ResourcePtr resource) {
  used_resources_.push_back(std::move(resource));
}

void CommandBuffer::DrawMesh(const MeshPtr& mesh) {
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

bool CommandBuffer::Retire() {
  if (!is_active_ && !is_submitted_) {
    // Submission failed, so proceed with cleanup.
  } else {
    FTL_DCHECK(is_active_ && is_submitted_);
    // Check if fence has been reached.
    auto fence_status = device_.getFenceStatus(fence_);
    if (fence_status == vk::Result::eNotReady) {
      // Fence has not been reached; try again later.
      return false;
    }
  }
  is_active_ = is_submitted_ = false;
  device_.resetFences(1, &fence_);

  used_resources_.clear();

  if (callback_) {
    callback_();
    callback_ = nullptr;
  }

  // TODO: move semaphores to pool for reuse?
  wait_semaphores_.clear();
  wait_semaphores_for_submit_.clear();
  wait_semaphore_stages_.clear();
  signal_semaphores_.clear();
  signal_semaphores_for_submit_.clear();

  auto result = command_buffer_.reset(vk::CommandBufferResetFlags());
  FTL_DCHECK(result == vk::Result::eSuccess);

  return true;
}

}  // namespace impl
}  // namespace escher
