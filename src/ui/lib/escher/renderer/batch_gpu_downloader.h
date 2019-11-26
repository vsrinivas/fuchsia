// Copyrisnapshotter.ccght 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_
#define SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_

#include <lib/fit/function.h>

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
  static std::unique_ptr<BatchGpuDownloader> New(
      EscherWeakPtr weak_escher,
      CommandBuffer::Type command_buffer_type = CommandBuffer::Type::kGraphics,
      uint64_t frame_trace_number = 0);

  BatchGpuDownloader(EscherWeakPtr weak_escher,
                     CommandBuffer::Type command_buffer_type = CommandBuffer::Type::kGraphics,
                     uint64_t frame_trace_number = 0);
  ~BatchGpuDownloader();

  // Returns true if the BatchGpuDownloader has acquired a reader and has work
  // to do on the GPU.
  bool HasContentToDownload() const { return is_initialized_; }

  // Provides a pointer in host-accessible GPU memory, and methods to copy into
  // this memory from Images and Buffers on the GPU.

  // TODO(41297): Remove Reader from BatchGpuDownloader; we can have one single
  // buffer for all the downloads, and let BatchGpuDownloader manage all the
  // downloads instead.
  class Reader {
   public:
    Reader(CommandBufferPtr command_buffer, BufferPtr buffer);
    ~Reader();

    // Schedule a buffer-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the source until the submission's
    // CommandBuffer is retired. Places a wait semaphore on the source,
    // which is signaled when the batched commands are done.
    void ReadBuffer(const BufferPtr& source, vk::BufferCopy region);

    // Schedule a image-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the source until the submission's
    // CommandBuffer is retired. Places a wait semaphore on the source,
    // which is signaled when the batched commands are done.
    void ReadImage(const ImagePtr& source, vk::BufferImageCopy region);

    const BufferPtr buffer() { return buffer_; }

   private:
    friend class BatchGpuDownloader;
    // Gets the CommandBuffer to batch commands with all other posted readers.
    // This reader cannot be used after the command buffer has been retrieved.
    CommandBufferPtr TakeCommandsAndShutdown();

    CommandBufferPtr command_buffer_;
    BufferPtr buffer_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
  };

  // Obtain a Reader that has the specified amount of space to read into.
  std::unique_ptr<Reader> AcquireReader(vk::DeviceSize size);

  // Post a Reader to the batch uploader. The Reader's work will be posted to
  // the host on Submit(). After submit, the callback will be called with the
  // buffer read from the GPU.
  // Note that callback will always be called even if there was no ReadBuffer()
  // or ReadImage() called before.
  void PostReader(std::unique_ptr<Reader> reader,
                  fit::function<void(escher::BufferPtr buffer)> callback);

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
  void Initialize();

  int32_t reader_count_ = 0;

  EscherWeakPtr escher_;

  CommandBuffer::Type command_buffer_type_ = CommandBuffer::Type::kTransfer;
  bool is_initialized_ = false;
  // The trace number for the frame. Cached to support lazy frame creation.
  const uint64_t frame_trace_number_;
  // Lazily created when the first Reader is acquired.
  BufferCacheWeakPtr buffer_cache_;
  FramePtr frame_;

  std::vector<std::pair<BufferPtr, fit::function<void(BufferPtr)>>> read_callbacks_;
  std::vector<std::pair<SemaphorePtr, vk::PipelineStageFlags>> wait_semaphores_;
  std::vector<SemaphorePtr> signal_semaphores_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchGpuDownloader);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_BATCH_GPU_DOWNLOADER_H_
