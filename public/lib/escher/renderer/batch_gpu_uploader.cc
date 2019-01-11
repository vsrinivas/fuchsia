// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/batch_gpu_uploader.h"

#include <lib/fit/function.h>

#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/fxl/logging.h"

namespace escher {

/* static */
BatchGpuUploaderPtr BatchGpuUploader::New(EscherWeakPtr weak_escher,
                                          int64_t frame_trace_number) {
  if (!weak_escher) {
    // Allow creation without an escher for tests. This class is not functional
    // without a valid escher.
    FXL_LOG(WARNING) << "Error, creating a BatchGpuUploader without an escher.";
    return fxl::AdoptRef(new BatchGpuUploader());
  }
  return fxl::AdoptRef(
      new BatchGpuUploader(std::move(weak_escher), frame_trace_number));
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
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Writer::WriteBuffer");
  if (semaphore) {
    command_buffer_->impl()->TakeWaitSemaphore(
        target, vk::PipelineStageFlagBits::eTransfer);
    target->SetWaitSemaphore(semaphore);
    command_buffer_->impl()->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->impl()->KeepAlive(target);
  command_buffer_->vk().copyBuffer(buffer_->vk(), target->vk(), 1, &region);
}

void BatchGpuUploader::Writer::WriteImage(const ImagePtr& target,
                                          vk::BufferImageCopy region,
                                          SemaphorePtr semaphore) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Writer::WriteImage");
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
    command_buffer_->impl()->TakeWaitSemaphore(
        target, vk::PipelineStageFlagBits::eTransfer);
    target->SetWaitSemaphore(semaphore);
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

BatchGpuUploader::Reader::Reader(CommandBufferPtr command_buffer,
                                 BufferPtr buffer)
    : command_buffer_(std::move(command_buffer)), buffer_(std::move(buffer)) {
  FXL_DCHECK(command_buffer_ && buffer_);
}

BatchGpuUploader::Reader::~Reader() {
  FXL_DCHECK(!command_buffer_ && !buffer_);
}

void BatchGpuUploader::Reader::ReadBuffer(const BufferPtr& source,
                                          vk::BufferCopy region,
                                          SemaphorePtr semaphore) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Reader::ReadBuffer");
  if (semaphore) {
    command_buffer_->impl()->TakeWaitSemaphore(
        source, vk::PipelineStageFlagBits::eTransfer);
    source->SetWaitSemaphore(semaphore);
    command_buffer_->impl()->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->vk().copyBuffer(source->vk(), buffer_->vk(), 1, &region);
  command_buffer_->impl()->KeepAlive(source);
}

void BatchGpuUploader::Reader::ReadImage(const ImagePtr& source,
                                         vk::BufferImageCopy region,
                                         SemaphorePtr semaphore) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::Reader::ReadImage");
  command_buffer_->impl()->TransitionImageLayout(
      source, vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageLayout::eTransferSrcOptimal);
  command_buffer_->vk().copyImageToBuffer(source->vk(),
                                          vk::ImageLayout::eTransferSrcOptimal,
                                          buffer_->vk(), 1, &region);
  command_buffer_->impl()->TransitionImageLayout(
      source, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  if (semaphore) {
    command_buffer_->impl()->TakeWaitSemaphore(
        source, vk::PipelineStageFlagBits::eTransfer);
    source->SetWaitSemaphore(semaphore);
    command_buffer_->impl()->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->impl()->KeepAlive(source);
}

CommandBufferPtr BatchGpuUploader::Reader::TakeCommandsAndShutdown() {
  FXL_DCHECK(command_buffer_);
  // Assume that if a reader was requested, it was read from, and the buffer
  // needs to be kept alive.
  command_buffer_->impl()->KeepAlive(std::move(buffer_));

  // Underlying CommandBuffer is being removed, shutdown this reader.
  buffer_ = nullptr;
  return std::move(command_buffer_);
}

BatchGpuUploader::BatchGpuUploader(EscherWeakPtr weak_escher,
                                   int64_t frame_trace_number)
    : escher_(std::move(weak_escher)), frame_trace_number_(frame_trace_number) {
  FXL_DCHECK(escher_);
}

BatchGpuUploader::~BatchGpuUploader() { FXL_CHECK(!frame_); }

void BatchGpuUploader::Initialize() {
  // TODO(ES-115) Back the uploader with transfer queue command buffers
  // directly, rather than use a frame to manage GPU submits, when command
  // buffer recycling is refactored.
  if (!frame_) {
    frame_ = escher_->NewFrame("Gpu Uploader", frame_trace_number_,
                               /* enable_gpu_logging */ false,
                               CommandBuffer::Type::kTransfer);
  }
  FXL_DCHECK(frame_);
  if (!buffer_cache_) {
    buffer_cache_ = escher_->buffer_cache()->GetWeakPtr();
  }
  FXL_DCHECK(buffer_cache_);

  is_initialized_ = true;
}

std::unique_ptr<BatchGpuUploader::Writer> BatchGpuUploader::AcquireWriter(
    size_t size) {
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
  return std::make_unique<BatchGpuUploader::Writer>(std::move(command_buffer),
                                                    std::move(buffer));
}

std::unique_ptr<BatchGpuUploader::Reader> BatchGpuUploader::AcquireReader(
    size_t size) {
  if (!is_initialized_) {
    Initialize();
  }
  FXL_DCHECK(frame_);
  FXL_DCHECK(size);
  // TODO(SCN-846) Relax this check once Readers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Reader.
  FXL_DCHECK(reader_count_ == 0);

  TRACE_DURATION("gfx", "escher::BatchGpuUploader::AcquireReader");

  vk::DeviceSize vk_size = size;
  BufferPtr buffer = buffer_cache_->NewHostBuffer(vk_size);
  FXL_DCHECK(buffer) << "Error allocating buffer";

  CommandBufferPtr command_buffer = frame_->TakeCommandBuffer();
  FXL_DCHECK(command_buffer) << "Error getting the frame's command buffer.";

  ++reader_count_;
  return std::make_unique<BatchGpuUploader::Reader>(std::move(command_buffer),
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

void BatchGpuUploader::PostReader(
    std::unique_ptr<BatchGpuUploader::Reader> reader,
    fit::function<void(escher::BufferPtr buffer)> callback) {
  FXL_DCHECK(frame_);
  if (!reader) {
    return;
  }
  read_callbacks_.push_back(
      std::make_pair(reader->buffer(), std::move(callback)));

  // TODO(SCN-846) Relax this check once Readers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Reader.
  FXL_DCHECK(reader_count_ == 1);

  auto command_buffer = reader->TakeCommandsAndShutdown();
  frame_->PutCommandBuffer(std::move(command_buffer));
  --reader_count_;
  reader.reset();
}

void BatchGpuUploader::Submit(const escher::SemaphorePtr& upload_done_semaphore,
                              fit::function<void()> callback) {
  if (dummy_for_tests_) {
    FXL_LOG(WARNING) << "Dummy BatchGpuUploader for tests, skip submit";
    return;
  }

  // TODO(SCN-846) Relax this check once Writers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Writer.
  FXL_DCHECK(writer_count_ == 0 && reader_count_ == 0);

  if (!is_initialized_) {
    // This uploader was never used, nothing to submit.
    return;
  }
  FXL_DCHECK(frame_);

  TRACE_DURATION("gfx", "BatchGpuUploader::SubmitBatch");
  frame_->EndFrame(upload_done_semaphore,
                   [callback = std::move(callback),
                    read_callbacks = std::move(read_callbacks_)]() {
                     for (auto& pair : read_callbacks) {
                       auto buffer = pair.first;
                       pair.second(buffer);
                     }
                     callback();
                   });
  frame_ = nullptr;
}

}  // namespace escher
