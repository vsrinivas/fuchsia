// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/batch_gpu_uploader.h"

#include "lib/escher/util/trace_macros.h"

namespace escher {

/* static */
BatchGpuUploader BatchGpuUploader::Create(EscherWeakPtr weak_escher,
                                          int64_t frame_trace_number) {
  if (!weak_escher) {
    // Allow creation without an escher for tests. This class is not functional
    // without a valid escher.
    FXL_LOG(WARNING) << "Error, creating a BatchGpuUploader without an escher.";
    return BatchGpuUploader();
  }

  BufferCacheWeakPtr buffer_cache = weak_escher->buffer_cache()->GetWeakPtr();
  FramePtr frame = weak_escher->NewFrame("Gpu Uploader", frame_trace_number,
                                         /* enable_gpu_logging */ false,
                                         CommandBuffer::Type::kTransfer);
  return BatchGpuUploader(std::move(buffer_cache), std::move(frame));
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

BatchGpuUploader::BatchGpuUploader(BufferCacheWeakPtr weak_buffer_cache,
                                   FramePtr frame)
    : buffer_cache_(std::move(weak_buffer_cache)), frame_(std::move(frame)) {
  FXL_DCHECK(buffer_cache_);
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

  vk::DeviceSize vk_size = size;
  BufferPtr buffer = buffer_cache_->NewHostBuffer(vk_size);
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
  if (dummy_for_tests_) {
    FXL_LOG(WARNING) << "Dummy BatchGpuUploader for tests, skip submit";
    return;
  }

  FXL_DCHECK(frame_);
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0);

  TRACE_DURATION("gfx", "BatchGpuUploader::SubmitBatch");
  frame_->EndFrame(upload_done_semaphore, callback);
  frame_ = nullptr;
}

}  // namespace escher
