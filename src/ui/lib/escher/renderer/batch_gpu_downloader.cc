// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"

#include <lib/fit/function.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {

std::unique_ptr<BatchGpuDownloader> BatchGpuDownloader::New(EscherWeakPtr weak_escher,
                                                            CommandBuffer::Type command_buffer_type,
                                                            uint64_t frame_trace_number) {
  if (!weak_escher) {
    // This class is not functional without a valid escher.
    FXL_LOG(WARNING) << "Error, creating a BatchGpuDownloader without an escher.";
    return nullptr;
  }
  return std::make_unique<BatchGpuDownloader>(std::move(weak_escher), command_buffer_type,
                                              frame_trace_number);
}

BatchGpuDownloader::Reader::Reader(CommandBufferPtr command_buffer, BufferPtr buffer)
    : command_buffer_(std::move(command_buffer)), buffer_(std::move(buffer)) {
  FXL_DCHECK(command_buffer_ && buffer_);
}

BatchGpuDownloader::Reader::~Reader() { FXL_DCHECK(!command_buffer_ && !buffer_); }

void BatchGpuDownloader::Reader::ReadBuffer(const BufferPtr& source, vk::BufferCopy region) {
  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::Reader::ReadBuffer");

  command_buffer_->vk().copyBuffer(source->vk(), buffer_->vk(), 1, &region);
  command_buffer_->KeepAlive(source);
}

void BatchGpuDownloader::Reader::ReadImage(const ImagePtr& source, vk::BufferImageCopy region) {
  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::Reader::ReadImage");

  auto source_layout = source->layout();
  auto target_layout =
      source->is_layout_initialized() ? source->layout() : vk::ImageLayout::eShaderReadOnlyOptimal;

  command_buffer_->TransitionImageLayout(source, source_layout,
                                         vk::ImageLayout::eTransferSrcOptimal);
  command_buffer_->vk().copyImageToBuffer(source->vk(), vk::ImageLayout::eTransferSrcOptimal,
                                          buffer_->vk(), 1, &region);
  command_buffer_->TransitionImageLayout(source, vk::ImageLayout::eTransferSrcOptimal,
                                         target_layout);
  command_buffer_->KeepAlive(source);
}

CommandBufferPtr BatchGpuDownloader::Reader::TakeCommandsAndShutdown() {
  FXL_DCHECK(command_buffer_);
  // Assume that if a reader was requested, it was read from, and the buffer
  // needs to be kept alive.
  command_buffer_->KeepAlive(buffer_);

  // Underlying CommandBuffer is being removed, shutdown this writer.
  buffer_ = nullptr;
  return std::move(command_buffer_);
}

BatchGpuDownloader::BatchGpuDownloader(EscherWeakPtr weak_escher,
                                       CommandBuffer::Type command_buffer_type,
                                       uint64_t frame_trace_number)
    : escher_(std::move(weak_escher)),
      command_buffer_type_(command_buffer_type),
      frame_trace_number_(frame_trace_number) {
  FXL_DCHECK(escher_);
}

BatchGpuDownloader::~BatchGpuDownloader() { FXL_CHECK(!frame_); }

void BatchGpuDownloader::Initialize() {
  // TODO(ES-115) Back the downloader with transfer queue command buffers
  // directly, rather than use a frame to manage GPU submits, when command
  // buffer recycling is refactored.
  if (!frame_) {
    frame_ = escher_->NewFrame("Gpu Downloader", frame_trace_number_,
                               /*enable_gpu_logging=*/false, command_buffer_type_,
                               /*use_protected_memory=*/false);
  }
  FXL_DCHECK(frame_);
  if (!buffer_cache_) {
    buffer_cache_ = escher_->buffer_cache()->GetWeakPtr();
  }
  FXL_DCHECK(buffer_cache_);

  is_initialized_ = true;
}

std::unique_ptr<BatchGpuDownloader::Reader> BatchGpuDownloader::AcquireReader(vk::DeviceSize size) {
  if (!is_initialized_) {
    Initialize();
  }
  FXL_DCHECK(frame_);
  FXL_DCHECK(size);
  // TODO(SCN-846) Relax this check once Readers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Reader.
  FXL_DCHECK(reader_count_ == 0);

  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::AcquireReader");

  vk::DeviceSize vk_size = size;
  BufferPtr buffer = buffer_cache_->NewHostBuffer(vk_size);
  FXL_DCHECK(buffer) << "Error allocating buffer";

  CommandBufferPtr command_buffer = frame_->TakeCommandBuffer();
  FXL_DCHECK(command_buffer) << "Error getting the frame's command buffer.";

  ++reader_count_;
  return std::make_unique<BatchGpuDownloader::Reader>(std::move(command_buffer), std::move(buffer));
}

void BatchGpuDownloader::PostReader(std::unique_ptr<BatchGpuDownloader::Reader> reader,
                                    fit::function<void(escher::BufferPtr buffer)> callback) {
  FXL_DCHECK(frame_);
  if (!reader) {
    return;
  }
  read_callbacks_.push_back(std::make_pair(reader->buffer(), std::move(callback)));

  // TODO(SCN-846) Relax this check once Readers are backed by secondary
  // buffers, and the frame's primary command buffer is not moved into the
  // Reader.
  FXL_DCHECK(reader_count_ == 1);

  auto command_buffer = reader->TakeCommandsAndShutdown();
  frame_->PutCommandBuffer(std::move(command_buffer));
  --reader_count_;
  reader.reset();
}

void BatchGpuDownloader::Submit(fit::function<void()> callback) {
  FXL_DCHECK(reader_count_ == 0);
  if (!is_initialized_) {
    // This downloader was never used, nothing to submit.
    if (callback) {
      callback();
    }
    return;
  }
  FXL_DCHECK(frame_);

  TRACE_DURATION("gfx", "BatchGpuDownloader::SubmitBatch");
  frame_->EndFrame(SemaphorePtr(),
                   [callback = std::move(callback), read_callbacks = std::move(read_callbacks_)]() {
                     for (auto& [buffer, read_callback_function] : read_callbacks) {
                       read_callback_function(buffer);
                     }
                     if (callback) {
                       callback();
                     }
                   });
  frame_ = nullptr;
}

}  // namespace escher
