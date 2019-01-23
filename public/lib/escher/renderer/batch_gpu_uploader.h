// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_
#define LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_

#include <vulkan/vulkan.hpp>

#include <lib/fit/function.h>

#include "lib/escher/base/reffable.h"
#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/buffer_cache.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/command_buffer.h"

namespace escher {

class BatchGpuUploader;
using BatchGpuUploaderPtr = fxl::RefPtr<BatchGpuUploader>;

// Provides host-accessible GPU memory for clients to upload Images and Buffers
// to the GPU. Offers the ability to batch uploads into consolidated submissions
// to the GPU driver.
//
// TODO(SCN-844) Migrate users of impl::GpuUploader to this class.
//
// TODO (SCN-1197) Add memory barriers so the BatchGpuUploader can handle
// reads and writes on the same Resource in the same batch.
class BatchGpuUploader : public Reffable {
 public:
  static BatchGpuUploaderPtr New(EscherWeakPtr weak_escher,
                                 int64_t frame_trace_number = 0);

  ~BatchGpuUploader();

  // Provides a pointer in host-accessible GPU memory, and methods to copy this
  // memory into optimally-formatted Images and Buffers.
  class Writer {
   public:
    Writer(CommandBufferPtr command_buffer, BufferPtr buffer,
           SemaphorePtr batch_done_semaphore);
    ~Writer();

    // Schedule a buffer-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired. Places the "BatchDone" Semaphore on the target,
    // which is signaled when the batched commands are done.
    void WriteBuffer(const BufferPtr& target, vk::BufferCopy region);

    // Schedule a buffer-to-image copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired. Places the "BatchDone" Semaphore on the target,
    // which is signaled when the batched commands are done.
    void WriteImage(const ImagePtr& target, vk::BufferImageCopy region);

    uint8_t* host_ptr() const { return buffer_->host_ptr(); }
    vk::DeviceSize size() const { return buffer_->size(); }

   private:
    friend class BatchGpuUploader;
    // Gets the CommandBuffer to batch commands with all other posted writers.
    // This writer cannot be used after the command buffer has been retrieved.
    CommandBufferPtr TakeCommandsAndShutdown();

    CommandBufferPtr command_buffer_;
    BufferPtr buffer_;
    SemaphorePtr batch_done_semaphore_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Writer);
  };

  // Provides a pointer in host-accessible GPU memory, and methods to copy into
  // this memory from Images and Buffers on the GPU.
  class Reader {
   public:
    Reader(CommandBufferPtr command_buffer, BufferPtr buffer,
           SemaphorePtr batch_done_semaphore);
    ~Reader();

    // Schedule a buffer-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the source until the submission's
    // CommandBuffer is retired. Places the "BatchDone" Semaphore on the source,
    // which is signaled when the batched commands are done.
    void ReadBuffer(const BufferPtr& source, vk::BufferCopy region);

    // Schedule a image-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the source until the submission's
    // CommandBuffer is retired. Places the "BatchDone" Semaphore on the source,
    // which is signaled when the batched commands are done.
    void ReadImage(const ImagePtr& source, vk::BufferImageCopy region);

    const BufferPtr buffer() { return buffer_; }

   private:
    friend class BatchGpuUploader;
    // Gets the CommandBuffer to batch commands with all other posted writers.
    // This writer cannot be used after the command buffer has been retrieved.
    CommandBufferPtr TakeCommandsAndShutdown();

    CommandBufferPtr command_buffer_;
    BufferPtr buffer_;
    SemaphorePtr batch_done_semaphore_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
  };

  // Obtain a Writer that has the specified amount of write space.
  //
  // TODO(SCN-846) Only one writer can be acquired at a time. When we move to
  // backing writers with secondary CommandBuffers, multiple writes can be
  // acquired at once and their writes can be parallelized across threads.
  std::unique_ptr<Writer> AcquireWriter(size_t size);

  // Obtain a Reader that has the specified amount of space to read into.
  std::unique_ptr<Reader> AcquireReader(size_t size);

  // Post a Writer to the batch uploader. The Writer's work will be posted to
  // the GPU on Submit();
  void PostWriter(std::unique_ptr<Writer> writer);

  // Post a Reader to the batch uploader. The Reader's work will be posted to
  // the host on Submit(). After submit, the callback will be called with the
  // buffer read from the GPU.
  void PostReader(std::unique_ptr<Reader> reader,
                  fit::function<void(escher::BufferPtr buffer)> callback);

  // The "BatchDone" Semaphore that is signaled when all posted batches are
  // complete. This is non-null after the first Reader or Writer is acquired,
  // and before Submit().
  SemaphorePtr GetBatchDoneSemaphore() {
    return batched_commands_done_semaphore_;
  }

  // Submits all Writers' and Reader's work to the GPU. No Writers or Readers
  // can be posted once Submit is called.
  void Submit(fit::function<void()> callback = nullptr);

 private:
  BatchGpuUploader(EscherWeakPtr weak_escher, int64_t frame_trace_number);
  BatchGpuUploader() : frame_trace_number_(0) { dummy_for_tests_ = true; }

  static void SemaphoreAssignmentHelper(SemaphorePtr batch_done_semaphore,
                                        WaitableResource* resource,
                                        CommandBuffer* command_buffer);

  void Initialize();

  int32_t writer_count_ = 0;
  int32_t reader_count_ = 0;

  EscherWeakPtr escher_;
  bool is_initialized_ = false;
  // The trace number for the frame. Cached to support lazy frame creation.
  const int64_t frame_trace_number_;
  // Lazily created when the first Reader or Writer is acquired.
  BufferCacheWeakPtr buffer_cache_;
  FramePtr frame_;
  SemaphorePtr batched_commands_done_semaphore_;

  // Temporary flag for tests that need to build and run with a null escher.
  // Allows the uploader to be created and skips submit without crashing, but
  // when this flag is set, BatchGpuUploader is not functional and does not
  // provide any dummy functionality.
  bool dummy_for_tests_ = false;

  std::vector<std::pair<BufferPtr, fit::function<void(BufferPtr)>>>
      read_callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchGpuUploader);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_
