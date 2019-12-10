// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"

#include <lib/fit/function.h>

#include <variant>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/align.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

namespace {

// Vulkan specs requires that bufferOffset in VkBufferImageCopy be multiple of
// 4, so we enforce that all buffer offsets (including buffers and images) align
// to multiples of 4 by aligning up the addresses.
constexpr vk::DeviceSize kByteAlignment = 4;

}  // namespace

std::unique_ptr<BatchGpuUploader> BatchGpuUploader::New(EscherWeakPtr weak_escher,
                                                        uint64_t frame_trace_number) {
  if (!weak_escher) {
    // This class is not functional without a valid escher.
    FXL_LOG(WARNING) << "Error, creating a BatchGpuUploader without an escher.";
    return nullptr;
  }
  return std::make_unique<BatchGpuUploader>(std::move(weak_escher), frame_trace_number);
}

BatchGpuUploader::BatchGpuUploader(EscherWeakPtr weak_escher, uint64_t frame_trace_number)
    : escher_(std::move(weak_escher)),
      frame_trace_number_(frame_trace_number),
      buffer_cache_(escher_->buffer_cache()->GetWeakPtr()) {
  FXL_DCHECK(escher_);
  FXL_DCHECK(buffer_cache_);
}

BatchGpuUploader::~BatchGpuUploader() {
  FXL_CHECK(resources_.empty() && copy_info_records_.empty() && wait_semaphores_.empty() &&
            signal_semaphores_.empty() && current_offset_ == 0U);
}

void BatchGpuUploader::ScheduleWriteBuffer(const BufferPtr& target,
                                           DataProviderCallback write_function,
                                           vk::DeviceSize target_offset, vk::DeviceSize copy_size) {
  TRACE_DURATION("gfx", "escher::BatchGpuUploader::ScheduleWriteBuffer");
  vk::DeviceSize src_offset = AlignedToNext(current_offset_, kByteAlignment);
  auto writeable_size = target->size() - target_offset;
  FXL_DCHECK(writeable_size >= copy_size) << "copy_size + target_offset exceeds the buffer size";
  auto write_size = std::min(copy_size, writeable_size);
  auto region = vk::BufferCopy(src_offset, target_offset, write_size);

  copy_info_records_.push_back(
      CopyInfo{.type = CopyType::COPY_BUFFER,
               .offset = src_offset,
               .size = write_size,
               .write_function = std::move(write_function),
               .copy_info = BufferCopyInfo{.target = target, .region = region}});
  current_offset_ = src_offset + copy_info_records_.back().size;

  // Keep the target alive until Submit().
  resources_.push_back(target);
}

void BatchGpuUploader::ScheduleWriteImage(const ImagePtr& target,
                                          DataProviderCallback write_function,
                                          vk::ImageLayout final_layout,
                                          vk::BufferImageCopy region) {
  // Create default buffer image copy if the region is empty.
  if (region == vk::BufferImageCopy()) {
    region = impl::GetDefaultBufferImageCopy(target->width(), target->height());
  }

  FXL_DCHECK(region.bufferOffset == 0U);

  // For now we expect that we only accept full image to be uploadable.
  FXL_DCHECK(region.imageOffset == vk::Offset3D(0, 0, 0) &&
             region.imageExtent == vk::Extent3D(target->width(), target->height(), 1U));

  TRACE_DURATION("gfx", "escher::BatchGpuUploader::ScheduleWriteImage");
  vk::DeviceSize src_offset = AlignedToNext(current_offset_, kByteAlignment);
  auto final_region = region;
  final_region.setBufferOffset(src_offset);

  copy_info_records_.push_back(CopyInfo{
      .type = CopyType::COPY_IMAGE,
      .offset = src_offset,
      .size = target->size(),
      .write_function = std::move(write_function),
      .copy_info =
          ImageCopyInfo{.target = target, .region = final_region, .final_layout = final_layout}});
  current_offset_ = src_offset + copy_info_records_.back().size;

  // Keep the target alive until Submit().
  resources_.push_back(target);
}

BufferPtr BatchGpuUploader::CreateBufferFromRecords() {
  FXL_DCHECK(buffer_cache_);
  FXL_DCHECK(HasContentToUpload());

  vk::DeviceSize buffer_size = copy_info_records_.back().offset + copy_info_records_.back().size;
  BufferPtr src_buffer = buffer_cache_->NewHostBuffer(buffer_size);
  FXL_DCHECK(src_buffer) << "Error allocating buffer";

  for (const auto& copy_info_record : copy_info_records_) {
    auto dst_ptr = src_buffer->host_ptr() + copy_info_record.offset;
    auto copy_size = copy_info_record.size;
    copy_info_record.write_function(dst_ptr, copy_size);
  }

  return src_buffer;
}

void BatchGpuUploader::GenerateCommands(CommandBuffer* cmds) {
  if (!NeedsCommandBuffer()) {
    return;
  }

  TRACE_DURATION("gfx", "BatchGpuUploader::GenerateCommands");
  // Check existence of command buffer and buffer cache.
  FXL_DCHECK(cmds);

  // We only create the src_buffer if we need to upload something.
  // If we only need to create an empty command buffer (to signal / wait on
  // semaphores), we won't need the buffer.
  BufferPtr src_buffer = BufferPtr();
  if (HasContentToUpload()) {
    src_buffer = CreateBufferFromRecords();
    cmds->KeepAlive(src_buffer);
  }

  // Set up pipeline flags and access flags for synchronization. See class
  // comments for details.
  constexpr auto kPipelineFlag = vk::PipelineStageFlagBits::eTransfer;
  const auto kAccessFlagOutside =
      vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
  const auto kAccessFlagInside = vk::AccessFlagBits::eTransferWrite;

  for (const auto& copy_info_record : copy_info_records_) {
    switch (copy_info_record.type) {
      case CopyType::COPY_IMAGE: {
        const auto* image_copy_info = std::get_if<ImageCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(image_copy_info);

        ImagePtr target = image_copy_info->target;
        auto final_layout = image_copy_info->final_layout;
        cmds->ImageBarrier(target, target->layout(), vk::ImageLayout::eTransferDstOptimal,
                           kPipelineFlag, kAccessFlagOutside, kPipelineFlag, kAccessFlagInside);
        cmds->vk().copyBufferToImage(src_buffer->vk(), target->vk(),
                                     vk::ImageLayout::eTransferDstOptimal, 1,
                                     &image_copy_info->region);
        cmds->ImageBarrier(target, vk::ImageLayout::eTransferDstOptimal, final_layout,
                           kPipelineFlag, kAccessFlagInside, kPipelineFlag, kAccessFlagOutside);
        cmds->KeepAlive(target);
        break;
      }
      case CopyType::COPY_BUFFER: {
        const auto* buffer_copy_info = std::get_if<BufferCopyInfo>(&copy_info_record.copy_info);
        FXL_DCHECK(buffer_copy_info);

        cmds->BufferBarrier(buffer_copy_info->target, kPipelineFlag, kAccessFlagOutside,
                            kPipelineFlag, kAccessFlagInside);
        cmds->vk().copyBuffer(src_buffer->vk(), buffer_copy_info->target->vk(), 1,
                              &buffer_copy_info->region);
        cmds->BufferBarrier(buffer_copy_info->target, kPipelineFlag, kAccessFlagInside,
                            kPipelineFlag, kAccessFlagOutside);

        cmds->KeepAlive(buffer_copy_info->target);
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
  copy_info_records_.clear();
  current_offset_ = 0U;
}

void BatchGpuUploader::Submit(fit::function<void()> callback) {
  if (!NeedsCommandBuffer()) {
    // This uploader was never used, nothing to submit.
    if (callback) {
      callback();
    }
    return;
  }

  TRACE_DURATION("gfx", "BatchGpuUploader::Submit");
  // Create new command buffer.
  auto frame = escher_->NewFrame("Gpu Uploader", frame_trace_number_,
                                 /*enable_gpu_logging=*/false, CommandBuffer::Type::kTransfer,
                                 /*use_protected_memory=*/false);

  // Add commands to |frame|'s command buffer.
  GenerateCommands(frame->cmds());

  // Submit the command buffer.
  frame->EndFrame(SemaphorePtr(), [callback = std::move(callback)]() {
    if (callback) {
      callback();
    }
  });

  // Verify that everything is reset so that the uploader can be used as though
  // it's new.
  FXL_CHECK(resources_.empty() && copy_info_records_.empty() && wait_semaphores_.empty() &&
            signal_semaphores_.empty() && current_offset_ == 0U);
}

void BatchGpuUploader::AddWaitSemaphore(SemaphorePtr sema, vk::PipelineStageFlags flags) {
  wait_semaphores_.push_back({std::move(sema), flags});
}

void BatchGpuUploader::AddSignalSemaphore(SemaphorePtr sema) {
  signal_semaphores_.push_back(std::move(sema));
}

}  // namespace escher
