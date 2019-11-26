// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"

#include <lib/fit/function.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {

std::unique_ptr<BatchGpuUploader> BatchGpuUploader::New(EscherWeakPtr weak_escher,
                                                        uint64_t frame_trace_number) {
  if (!weak_escher) {
    // This class is not functional without a valid escher.
    FXL_LOG(WARNING) << "Error, creating a BatchGpuUploader without an escher.";
    return nullptr;
  }
  return std::make_unique<BatchGpuUploader>(std::move(weak_escher), frame_trace_number);
}

BatchGpuUploader::Writer::Writer(CommandBufferPtr command_buffer, BufferPtr buffer)
    : command_buffer_(std::move(command_buffer)), buffer_(std::move(buffer)) {
  FXL_DCHECK(command_buffer_ && buffer_);
}

BatchGpuUploader::Writer::~Writer() { FXL_DCHECK(!command_buffer_ && !buffer_); }

void BatchGpuUploader::Writer::WriteBuffer(const BufferPtr& target, vk::BufferCopy region) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Writer::WriteBuffer");

  command_buffer_->vk().copyBuffer(buffer_->vk(), target->vk(), 1, &region);
  command_buffer_->KeepAlive(target);
}

void BatchGpuUploader::Writer::WriteImage(const ImagePtr& target, vk::BufferImageCopy region,
                                          vk::ImageLayout final_layout) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Writer::WriteImage");

  command_buffer_->TransitionImageLayout(target, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal);
  command_buffer_->vk().copyBufferToImage(buffer_->vk(), target->vk(),
                                          vk::ImageLayout::eTransferDstOptimal, 1, &region);
  if (final_layout != vk::ImageLayout::eTransferDstOptimal) {
    command_buffer_->TransitionImageLayout(target, vk::ImageLayout::eTransferDstOptimal,
                                           final_layout);
  }

  command_buffer_->KeepAlive(target);
}

CommandBufferPtr BatchGpuUploader::Writer::TakeCommandsAndShutdown() {
  FXL_DCHECK(command_buffer_);
  // Assume that if a writer was requested, it was written to, and the buffer
  // needs to be kept alive.
  command_buffer_->KeepAlive(buffer_);

  // Underlying CommandBuffer is being removed, shutdown this writer.
  buffer_ = nullptr;
  return std::move(command_buffer_);
}

BatchGpuUploader::BatchGpuUploader(EscherWeakPtr weak_escher, uint64_t frame_trace_number)
    : escher_(std::move(weak_escher)), frame_trace_number_(frame_trace_number) {
  FXL_DCHECK(escher_);
}

BatchGpuUploader::~BatchGpuUploader() {
  FXL_CHECK(!frame_);
  FXL_CHECK(writer_count_ == 0);
}

void BatchGpuUploader::Initialize() {
  // TODO(ES-115) Back the uploader with transfer queue command buffers
  // directly, rather than use a frame to manage GPU submits, when command
  // buffer recycling is refactored.
  if (!frame_) {
    frame_ = escher_->NewFrame("Gpu Uploader", frame_trace_number_,
                               /*enable_gpu_logging=*/false, CommandBuffer::Type::kTransfer,
                               /*use_protected_memory=*/false);
  }
  FXL_DCHECK(frame_);
  if (!buffer_cache_) {
    buffer_cache_ = escher_->buffer_cache()->GetWeakPtr();
  }
  FXL_DCHECK(buffer_cache_);

  is_initialized_ = true;
}

std::unique_ptr<BatchGpuUploader::Writer> BatchGpuUploader::AcquireWriter(vk::DeviceSize size) {
  if (!is_initialized_) {
    Initialize();
  }
  FXL_DCHECK(frame_);
  FXL_DCHECK(size);
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0);

  TRACE_DURATION("gfx", "escher::BatchGpuUploader::AcquireWriter");

  vk::DeviceSize vk_size = size;
  BufferPtr buffer = buffer_cache_->NewHostBuffer(vk_size);
  FXL_DCHECK(buffer) << "Error allocating buffer";

  CommandBufferPtr command_buffer = frame_->TakeCommandBuffer();
  FXL_DCHECK(command_buffer) << "Error getting the frame's command buffer.";

  ++writer_count_;
  return std::make_unique<BatchGpuUploader::Writer>(std::move(command_buffer), std::move(buffer));
}

void BatchGpuUploader::PostWriter(std::unique_ptr<BatchGpuUploader::Writer> writer) {
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

void BatchGpuUploader::Submit(fit::function<void()> callback) {
  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0);
  if (!is_initialized_) {
    // This uploader was never used, nothing to submit.
    if (callback) {
      callback();
    }
    return;
  }
  FXL_DCHECK(frame_);

  // Add semaphores for the submitted command buffer to wait on.
  for (auto& pair : wait_semaphores_) {
    frame_->cmds()->AddWaitSemaphore(std::move(pair.first), pair.second);
  }
  wait_semaphores_.clear();

  for (auto& sem : signal_semaphores_) {
    frame_->cmds()->AddSignalSemaphore(std::move(sem));
  }
  signal_semaphores_.clear();

  TRACE_DURATION("gfx", "BatchGpuUploader::SubmitBatch");
  frame_->EndFrame(SemaphorePtr(), [callback = std::move(callback)]() {
    if (callback) {
      callback();
    }
  });
  frame_ = nullptr;
}

void BatchGpuUploader::AddWaitSemaphore(SemaphorePtr sema, vk::PipelineStageFlags flags) {
  if (!is_initialized_) {
    Initialize();
  }
  wait_semaphores_.push_back({std::move(sema), flags});
}

void BatchGpuUploader::AddSignalSemaphore(SemaphorePtr sema) {
  if (!is_initialized_) {
    Initialize();
  }
  signal_semaphores_.push_back(std::move(sema));
}

}  // namespace escher
