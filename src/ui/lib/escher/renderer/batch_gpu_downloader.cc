// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"

#include <lib/fit/function.h>

#include <variant>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/align.h"
#include "src/ui/lib/escher/util/trace_macros.h"

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
      frame_trace_number_(frame_trace_number),
      buffer_cache_(escher_->buffer_cache()->GetWeakPtr()) {
  FXL_DCHECK(escher_);
  FXL_DCHECK(buffer_cache_);
}

BatchGpuDownloader::~BatchGpuDownloader() {
  FXL_CHECK(resources_.empty() && copy_info_records_.empty() && wait_semaphores_.empty() &&
            signal_semaphores_.empty() && current_offset_ == 0U);
}

void BatchGpuDownloader::ScheduleReadBuffer(const BufferPtr& source,
                                            BatchGpuDownloader::Callback callback,
                                            vk::DeviceSize source_offset,
                                            vk::DeviceSize copy_size) {
  TRACE_DURATION("gfx", "escher::BatchGpuDownloader::ScheduleReadBuffer");
  vk::DeviceSize dst_offset = AlignedToNext(current_offset_, kByteAlignment);
  copy_size = copy_size == 0U ? source->size() : copy_size;
  auto region = vk::BufferCopy(source_offset, dst_offset, copy_size);

  copy_info_records_.push_back(
      CopyInfo{.type = CopyType::COPY_BUFFER,
               .offset = dst_offset,
               .size = region.size,
               .callback = std::move(callback),
               .copy_info = BufferCopyInfo{.source = source, .region = region}});
  current_offset_ = dst_offset + copy_info_records_.back().size;
  resources_.push_back(source);
}

void BatchGpuDownloader::ScheduleReadImage(const ImagePtr& source,
                                           BatchGpuDownloader::Callback callback,
                                           vk::BufferImageCopy region) {
  if (region == vk::BufferImageCopy()) {
    region = impl::GetDefaultBufferImageCopy(source->width(), source->height());
  }
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
  resources_.push_back(source);
}

CommandBufferFinishedCallback BatchGpuDownloader::GenerateCommands(CommandBuffer* cmds) {
  if (!NeedsCommandBuffer()) {
    return []() {};
  }

  TRACE_DURATION("gfx", "BatchGpuDownloader::GenerateCommands");
  // Check existence of command buffer and buffer cache.
  FXL_DCHECK(cmds);

  // We only create the target_buffer if we need to download something.
  // If we only need to create an empty command buffer (to signal / wait on
  // semaphores), we won't need the buffer.
  BufferPtr target_buffer = BufferPtr();
  if (HasContentToDownload()) {
    // Create a large buffer to store all images / buffers to be downloaded.
    vk::DeviceSize buffer_size = copy_info_records_.back().offset + copy_info_records_.back().size;
    target_buffer = buffer_cache_->NewHostBuffer(buffer_size);
    FXL_DCHECK(target_buffer) << "Error allocating buffer";
    cmds->KeepAlive(target_buffer);
  }

  // Set up pipeline flags and access flags for synchronization. See class
  // comments for details.
  constexpr auto kPipelineFlag = vk::PipelineStageFlagBits::eTransfer;
  const auto kAccessFlagOutside =
      vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
  const auto kAccessFlagInside = vk::AccessFlagBits::eTransferRead;

  for (const auto& copy_info_record : copy_info_records_) {
    switch (copy_info_record.type) {
      case CopyType::COPY_IMAGE: {
        const auto* image_copy_info = std::get_if<ImageCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(image_copy_info);

        ImagePtr source = image_copy_info->source;
        auto target_layout = source->is_layout_initialized()
                                 ? source->layout()
                                 : vk::ImageLayout::eShaderReadOnlyOptimal;

        cmds->ImageBarrier(source, source->layout(), vk::ImageLayout::eTransferSrcOptimal,
                           kPipelineFlag, kAccessFlagOutside, kPipelineFlag, kAccessFlagInside);
        cmds->vk().copyImageToBuffer(source->vk(), vk::ImageLayout::eTransferSrcOptimal,
                                     target_buffer->vk(), 1, &image_copy_info->region);
        cmds->ImageBarrier(source, vk::ImageLayout::eTransferSrcOptimal, target_layout,
                           kPipelineFlag, kAccessFlagInside, kPipelineFlag, kAccessFlagOutside);
        cmds->KeepAlive(source);
        break;
      }
      case CopyType::COPY_BUFFER: {
        const auto* buffer_copy_info = std::get_if<BufferCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(buffer_copy_info);

        cmds->BufferBarrier(buffer_copy_info->source, kPipelineFlag, kAccessFlagOutside,
                            kPipelineFlag, kAccessFlagInside);
        cmds->vk().copyBuffer(buffer_copy_info->source->vk(), target_buffer->vk(), 1,
                              &buffer_copy_info->region);
        cmds->BufferBarrier(buffer_copy_info->source, kPipelineFlag, kAccessFlagInside,
                            kPipelineFlag, kAccessFlagOutside);
        cmds->KeepAlive(buffer_copy_info->source);
        break;
      }
    }
  }

  // Add semaphores for the submitted command buffer to wait on / signal.
  for (auto& pair : wait_semaphores_) {
    cmds->AddWaitSemaphore(std::move(pair.first), pair.second);
  }
  wait_semaphores_.clear();

  for (auto& sem : signal_semaphores_) {
    cmds->AddSignalSemaphore(std::move(sem));
  }
  signal_semaphores_.clear();

  // Since we keep the target alive using CommandBuffer, we can remove these
  // RefPtrs from |resources_| right now.
  resources_.clear();
  current_offset_ = 0U;

  // The target buffer will be moved to the callback function so it will be
  // still alive until this function gets called.
  // |copy_info_records_| is guaranteed to be cleared after being moved to
  // |readers_info|.
  return
      [target_buffer = std::move(target_buffer), readers_info = std::move(copy_info_records_)]() {
        for (const auto& reader_info : readers_info) {
          reader_info.callback(target_buffer->host_ptr() + reader_info.offset, reader_info.size);
        }
      };
}

void BatchGpuDownloader::Submit(CommandBufferFinishedCallback client_callback) {
  if (!NeedsCommandBuffer()) {
    // This downloader was never used, nothing to submit.
    if (client_callback) {
      client_callback();
    }
    return;
  }

  TRACE_DURATION("gfx", "BatchGpuDownloader::Submit");

  // Create new command buffer.
  FramePtr frame = escher_->NewFrame("Gpu Downloader", frame_trace_number_,
                                     /*enable_gpu_logging=*/false, command_buffer_type_,
                                     /*use_protected_memory=*/false);

  // Add commands to |frame|'s command buffer.
  CommandBufferFinishedCallback reader_callback = GenerateCommands(frame->cmds());

  // Submit the command buffer.
  frame->EndFrame(SemaphorePtr(), [client_callback = std::move(client_callback),
                                   reader_callback = std::move(reader_callback)]() {
    if (reader_callback) {
      reader_callback();
    }
    if (client_callback) {
      client_callback();
    }
  });

  // Verify that everything is reset so that the downloader can be reused as
  // though new.
  FXL_CHECK(resources_.empty() && copy_info_records_.empty() && wait_semaphores_.empty() &&
            signal_semaphores_.empty() && current_offset_ == 0U);
}

}  // namespace escher
