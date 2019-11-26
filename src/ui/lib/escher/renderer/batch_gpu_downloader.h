// Copyrisnapshotter.ccght 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_
#define SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_

#include <lib/fit/function.h>

#include <variant>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/renderer/buffer_cache.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// Provides host-accessible GPU memory for clients to download Images and
// Buffers from the GPU to host memory. Offers the ability to batch downloads
// into consolidated submissions to the GPU driver.
//
// TODO(SCN-1197) Add memory barriers so the BatchGpuUploader and
// BatchGpuDownloader can handle synchronzation of reads and writes on the same
// Resource.
//
// Currently users of BatchGpuDownloader should manually enforce that
// the BatchGpuDownloader waits on other BatchGpuUploader or gfx::Engine if they
// write to the images / buffers the BatchGpuDownloader reads from, by using
// AddWaitSemaphore() function. Also, Submit() function will return a semaphore
// being signaled when command buffer finishes execution, which can be used for
// synchronization.
class BatchGpuDownloader {
 public:
  using CallbackType = fit::function<void(const void* host_ptr, size_t size)>;

  static std::unique_ptr<BatchGpuDownloader> New(
      EscherWeakPtr weak_escher,
      CommandBuffer::Type command_buffer_type = CommandBuffer::Type::kGraphics,
      uint64_t frame_trace_number = 0);

  BatchGpuDownloader(EscherWeakPtr weak_escher,
                     CommandBuffer::Type command_buffer_type = CommandBuffer::Type::kGraphics,
                     uint64_t frame_trace_number = 0);
  ~BatchGpuDownloader();

  // Returns true if the BatchGpuDownloader has work to do on the GPU.
  bool HasContentToDownload() const { return !copy_info_records_.empty(); }

  // Schedule a buffer-to-buffer copy that will be submitted when Submit()
  // is called.  Retains a reference to the source until the submission's
  // CommandBuffer is retired.
  void ScheduleReadBuffer(const BufferPtr& source, vk::BufferCopy region, CallbackType callback);

  // Schedule a image-to-buffer copy that will be submitted when Submit()
  // is called.  Retains a reference to the source until the submission's
  // CommandBuffer is retired.
  void ScheduleReadImage(const ImagePtr& source, vk::BufferImageCopy region, CallbackType callback);

  // Submits all Reader's work to the GPU. No Readers can be posted once Submit
  // is called. Callback function will be called after all work is done.
  void Submit(fit::function<void()> callback = nullptr);

  // Submit() will wait on all semaphores added by AddWaitSemaphore().
  void AddWaitSemaphore(SemaphorePtr sema, vk::PipelineStageFlags flags) {
    if (!is_initialized_) {
      Initialize();
    }
    wait_semaphores_.push_back({std::move(sema), flags});
  }

  // Submit() will singal all semaphores added by AddSignalSemaphore().
  void AddSignalSemaphore(SemaphorePtr sema) {
    if (!is_initialized_) {
      Initialize();
    }
    signal_semaphores_.push_back(std::move(sema));
  }

 private:
  enum class CopyType { COPY_IMAGE = 0, COPY_BUFFER = 1 };
  struct ImageCopyInfo {
    ImagePtr source;
    vk::BufferImageCopy region;
  };
  struct BufferCopyInfo {
    BufferPtr source;
    vk::BufferCopy region;
  };
  using CopyInfoVariant = std::variant<ImageCopyInfo, BufferCopyInfo>;

  struct CopyInfo {
    CopyType type;
    vk::DeviceSize offset;
    vk::DeviceSize size;
    CallbackType callback;
    // copy_info can either be a ImageCopyInfo or a BufferCopyInfo.
    CopyInfoVariant copy_info;
  };

  void Initialize();

  // Push all pending commands to |command_buffer_| to copy all the buffers and
  // images we scheduled before to the target buffer.
  void CopyBuffersAndImagesToTargetBuffer(BufferPtr target_buffer);

  EscherWeakPtr escher_;

  CommandBuffer::Type command_buffer_type_ = CommandBuffer::Type::kTransfer;
  bool is_initialized_ = false;
  bool has_submitted_ = false;

  // The trace number for the frame. Cached to support lazy frame creation.
  const uint64_t frame_trace_number_;
  // Lazily created when the first Reader is acquired.
  BufferCacheWeakPtr buffer_cache_;
  FramePtr frame_;

  CommandBufferPtr command_buffer_;
  vk::DeviceSize current_offset_ = 0U;

  std::vector<CopyInfo> copy_info_records_;
  std::vector<std::pair<SemaphorePtr, vk::PipelineStageFlags>> wait_semaphores_;
  std::vector<SemaphorePtr> signal_semaphores_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchGpuDownloader);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_
