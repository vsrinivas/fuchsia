// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"

#include <lib/fit/function.h>

#include <variant>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/align.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {

namespace {

// Vulkan specs requires that bufferOffset in VkBufferImageCopy be multiple of
// 4, so we enforce that all buffer offsets (including buffers and images) align
// to multiples of 4 by aligning up the addresses.
constexpr vk::DeviceSize kByteAlignment = 4;

}  // namespace

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
  FXL_DCHECK(!has_submitted_);
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

  command_buffer_ = frame_->TakeCommandBuffer();
  FXL_DCHECK(command_buffer_) << "Error getting the frame's command buffer.";

  current_offset_ = 0U;
  is_initialized_ = true;
}

void BatchGpuDownloader::ScheduleReadBuffer(const BufferPtr& source, vk::BufferCopy region,
                                            BatchGpuDownloader::CallbackType callback) {
  if (!is_initialized_) {
    Initialize();
  }
  FXL_DCHECK(!has_submitted_);
  FXL_DCHECK(region.dstOffset == 0U);

  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::ScheduleReadBuffer");
  vk::DeviceSize dst_offset = AlignedToNext(current_offset_, kByteAlignment);
  auto final_region = vk::BufferCopy(region.srcOffset, dst_offset, region.size);

  copy_info_records_.push_back(
      CopyInfo{.type = CopyType::COPY_BUFFER,
               .offset = dst_offset,
               .size = region.size,
               .callback = std::move(callback),
               .copy_info = BufferCopyInfo{.source = source, .region = final_region}});
  current_offset_ = dst_offset + copy_info_records_.back().size;
  command_buffer_->KeepAlive(source);
}

void BatchGpuDownloader::ScheduleReadImage(const ImagePtr& source, vk::BufferImageCopy region,
                                           BatchGpuDownloader::CallbackType callback) {
  if (!is_initialized_) {
    Initialize();
  }
  FXL_DCHECK(!has_submitted_);
  FXL_DCHECK(region.bufferOffset == 0U);

  // For now we expect that we only accept full image to be downloadable.
  FXL_DCHECK(region.imageOffset == vk::Offset3D(0, 0, 0) &&
             region.imageExtent == vk::Extent3D(source->width(), source->height(), 1U));

  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::ScheduleReadImage");
  vk::DeviceSize dst_offset = AlignedToNext(current_offset_, kByteAlignment);
  auto final_region = region;
  final_region.setBufferOffset(dst_offset);

  copy_info_records_.push_back(
      CopyInfo{.type = CopyType::COPY_IMAGE,
               .offset = dst_offset,
               // TODO(add a bug) Use the size calculated from region.
               .size = source->size(),
               .callback = std::move(callback),
               .copy_info = ImageCopyInfo{.source = source, .region = final_region}});
  current_offset_ = dst_offset + copy_info_records_.back().size;
  command_buffer_->KeepAlive(source);
}

void BatchGpuDownloader::CopyBuffersAndImagesToTargetBuffer(BufferPtr target_buffer) {
  TRACE_DURATION("gfx", "BatchGpuDownloader::CopyBuffersAndImagesToTargetBuffer");

  for (const auto& copy_info_record : copy_info_records_) {
    switch (copy_info_record.type) {
      case CopyType::COPY_IMAGE: {
        const auto* image_copy_info = std::get_if<ImageCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(image_copy_info);

        ImagePtr source = image_copy_info->source;
        auto source_layout = source->layout();
        auto target_layout = source->is_layout_initialized()
                                 ? source->layout()
                                 : vk::ImageLayout::eShaderReadOnlyOptimal;

        command_buffer_->TransitionImageLayout(source, source_layout,
                                               vk::ImageLayout::eTransferSrcOptimal);
        command_buffer_->vk().copyImageToBuffer(source->vk(), vk::ImageLayout::eTransferSrcOptimal,
                                                target_buffer->vk(), 1, &image_copy_info->region);
        command_buffer_->TransitionImageLayout(source, vk::ImageLayout::eTransferSrcOptimal,
                                               target_layout);
        break;
      }
      case CopyType::COPY_BUFFER: {
        const auto* buffer_copy_info = std::get_if<BufferCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(buffer_copy_info);

        command_buffer_->vk().copyBuffer(buffer_copy_info->source->vk(), target_buffer->vk(), 1,
                                         &buffer_copy_info->region);
        break;
      }
    }
  }
}

void BatchGpuDownloader::Submit(fit::function<void()> callback) {
  if (!is_initialized_) {
    // This downloader was never used, nothing to submit.
    if (callback) {
      callback();
    }
    return;
  }
  FXL_DCHECK(!has_submitted_);
  FXL_DCHECK(frame_);

  TRACE_DURATION("gfx", "BatchGpuDownloader::Submit");

  // Create a large buffer to store all images / buffers to be downloaded.
  vk::DeviceSize buffer_size = copy_info_records_.back().offset + copy_info_records_.back().size;
  BufferPtr buffer = buffer_cache_->NewHostBuffer(buffer_size);
  FXL_DCHECK(buffer) << "Error allocating buffer";
  command_buffer_->KeepAlive(buffer);

  // Put all the copy commands to the command buffer, and send the command
  // buffer back to |frame_|.
  CopyBuffersAndImagesToTargetBuffer(buffer);
  frame_->PutCommandBuffer(std::move(command_buffer_));

  frame_->EndFrame(
      SemaphorePtr(), [callback = std::move(callback), readers_info = std::move(copy_info_records_),
                       buffer = std::move(buffer)]() {
        for (const auto& reader_info : readers_info) {
          reader_info.callback(buffer->host_ptr() + reader_info.offset, reader_info.size);
        }
        if (callback) {
          callback();
        }
      });
  frame_ = nullptr;
  has_submitted_ = true;
}

}  // namespace escher
