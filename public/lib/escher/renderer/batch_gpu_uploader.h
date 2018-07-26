// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_
#define LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/base/reffable.h"
#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/command_buffer.h"

namespace escher {

class BatchGpuUploader;
using BatchGpuUploaderPtr = fxl::RefPtr<BatchGpuUploader>;

// Provides host-accessible GPU memory for clients to upload Images and Buffers
// to the GPU. Offers the ability to batch uploads into consolidated submissions
// to the GPU driver.
// TODO(SCN-844) Migrate users of impl::GpuUploader to this class.
class BatchGpuUploader : public ResourceRecycler, public Reffable {
 public:
  static BatchGpuUploaderPtr New(EscherWeakPtr weak_escher,
                                 int64_t frame_trace_number = 0);

  ~BatchGpuUploader();

  // Provides a pointer in host-accessible GPU memory, and methods to copy this
  // memory into optimally-formatted Images and Buffers.
  class Writer {
   public:
    Writer(CommandBufferPtr command_buffer, BufferPtr buffer);
    ~Writer();

    // Schedule a buffer-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired.
    void WriteBuffer(const BufferPtr& target, vk::BufferCopy region,
                     SemaphorePtr semaphore);

    // Schedule a buffer-to-image copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired.
    void WriteImage(const ImagePtr& target, vk::BufferImageCopy region,
                    SemaphorePtr semaphore);

    uint8_t* host_ptr() const { return buffer_->ptr(); }
    vk::DeviceSize size() const { return buffer_->size(); }

   private:
    friend class BatchGpuUploader;
    // Gets the CommandBuffer to batch commands with all other posted writers.
    // This writer cannot be used after the command buffer has been retreived.
    CommandBufferPtr TakeCommandsAndShutdown();

    CommandBufferPtr command_buffer_;
    BufferPtr buffer_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Writer);
  };

  // Obtain a Writer that has the specified amount of write space.
  //
  // TODO(SCN-846) Only one writer can be acquired at a time. When we move to
  // backing writers with secondary CommandBuffers, multiple writes can be
  // acquired at once and their writes can be parallelized across threads.
  std::unique_ptr<Writer> AcquireWriter(size_t size);

  // Post a Writer to the batch uploader. The Writer's work will be posted to
  // the GPU on Submit();
  void PostWriter(std::unique_ptr<Writer> writer);

  // Submits all Writers' work to the GPU. No Writers can be posted once Submit
  // is called.
  void Submit(const escher::SemaphorePtr& upload_done_semaphore,
              const std::function<void()>& callback = [] {});

  // |ResourceRecycler|
  void RecycleResource(std::unique_ptr<Resource> resource) override;

 private:
  BatchGpuUploader(EscherWeakPtr weak_escher, FramePtr frame);

  int32_t writer_count_ = 0;
  FramePtr frame_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BatchGpuUploader);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_
