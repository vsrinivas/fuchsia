// Copyright 2018 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/batch_gpu_uploader.h"

#include <trace/event.h>

#include "lib/escher/util/bit_ops.h"

namespace escher {

/* static */
BatchGpuUploaderPtr BatchGpuUploader::New(EscherWeakPtr weak_escher,
                                          int64_t frame_trace_number) {
  FramePtr frame = weak_escher->NewFrame("Gpu Uploader", frame_trace_number,
                                         /* enable_gpu_logging */ false,
                                         CommandBuffer::Type::kTransfer);
  return fxl::AdoptRef(
      new BatchGpuUploader(std::move(weak_escher), std::move(frame)));
}

BatchGpuUploader::Writer::Writer(CommandBufferPtr command_buffer,
                                 BufferPtr buffer)
    : command_buffer_(std::move(command_buffer)), buffer_(std::move(buffer)) {
  FXL_DCHECK(command_buffer_ && buffer_);
}

BatchGpuUploader::Writer::~Writer() {
  FXL_DCHECK(!command_buffer_ && !buffer_);
}

void BatchGpuUploader::Writer::WriteBuffer(const BufferPtr& target,
                                           vk::BufferCopy region,
                                           SemaphorePtr semaphore) {
  if (semaphore) {
    target->SetWaitSemaphore(semaphore);
    command_buffer_->impl()->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->impl()->KeepAlive(target);
  command_buffer_->vk().copyBuffer(buffer_->vk(), target->vk(), 1, &region);
}

void BatchGpuUploader::Writer::WriteImage(const ImagePtr& target,
                                          vk::BufferImageCopy region,
                                          SemaphorePtr semaphore) {
  command_buffer_->impl()->TransitionImageLayout(
      target, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eTransferDstOptimal);
  command_buffer_->vk().copyBufferToImage(buffer_->vk(), target->vk(),
                                          vk::ImageLayout::eTransferDstOptimal,
                                          1, &region);
  command_buffer_->impl()->TransitionImageLayout(
      target, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  if (semaphore) {
    if (target->HasWaitSemaphore()) {
      target->ReplaceWaitSemaphore(semaphore);
    } else {
      target->SetWaitSemaphore(semaphore);
    }
    command_buffer_->impl()->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->impl()->KeepAlive(target);
}

CommandBufferPtr BatchGpuUploader::Writer::TakeCommandsAndShutdown() {
  FXL_DCHECK(command_buffer_);
  // Assume that if a writer was requested, it was written to, and the buffer
  // needs to be kept alive.
  command_buffer_->impl()->KeepAlive(std::move(buffer_));

  // Underlying CommandBuffer is being removed, shutdown this writer.
  buffer_ = nullptr;
  return std::move(command_buffer_);
}

BatchGpuUploader::BatchGpuUploader(EscherWeakPtr weak_escher, FramePtr frame)
    : ResourceRecycler(std::move(weak_escher)), frame_(frame) {
  FXL_DCHECK(frame_);
}

BatchGpuUploader::~BatchGpuUploader() { FXL_CHECK(!frame_); }

std::unique_ptr<BatchGpuUploader::Writer> BatchGpuUploader::AcquireWriter(
    size_t size) {
  FXL_DCHECK(frame_);
  FXL_DCHECK(size);
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0);

  BufferPtr buffer;
  vk::DeviceSize vk_size = size;
  if (auto allocator = frame_->gpu_allocator()) {
    // TODO(SCN-851) Get the Buffer from a host memory buffer pool, rather than
    // allocate a new buffer for each requested Writer.
    auto memory_properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;
    buffer =
        Buffer::New(this, allocator, vk_size,
                    vk::BufferUsageFlagBits::eTransferSrc, memory_properties);
  }
  FXL_DCHECK(buffer) << "Error allocating buffer";

  CommandBufferPtr command_buffer = frame_->TakeCommandBuffer();
  FXL_DCHECK(command_buffer) << "Error getting the frame's command buffer.";

  ++writer_count_;
  return std::make_unique<BatchGpuUploader::Writer>(std::move(command_buffer),
                                                    std::move(buffer));
}

void BatchGpuUploader::PostWriter(
    std::unique_ptr<BatchGpuUploader::Writer> writer) {
  FXL_DCHECK(frame_);
  if (!writer) {
    return;
  }
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 1);

  auto command_buffer = writer->TakeCommandsAndShutdown();
  frame_->PutCommandBuffer(std::move(command_buffer));
  --writer_count_;
  writer.reset();
}

void BatchGpuUploader::Submit(const escher::SemaphorePtr& upload_done_semaphore,
                              const std::function<void()>& callback) {
  FXL_DCHECK(frame_);
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0);

  TRACE_DURATION("gfx", "BatchGpuUploader::SubmitBatch");
  frame_->EndFrame(upload_done_semaphore, callback);
  frame_ = nullptr;
}

void BatchGpuUploader::RecycleResource(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Buffer>());
  // TODO(SCN-851) Recycle buffer into a pool, rather than releasing it
  // immediately.
}

}  // namespace escher
