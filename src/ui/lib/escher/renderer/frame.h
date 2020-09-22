// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_FRAME_H_
#define SRC_UI_LIB_ESCHER_RENDERER_FRAME_H_

#include <lib/fit/function.h>

#include <cstdint>
#include <functional>

#include "src/ui/lib/escher/base/reffable.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/profiling/timestamp_profiler.h"
#include "src/ui/lib/escher/renderer/uniform_block_allocator.h"
#include "src/ui/lib/escher/util/block_allocator.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

#include <vulkan/vulkan.hpp>

namespace escher {

typedef fit::function<void()> FrameRetiredCallback;

class Frame;
using FramePtr = fxl::RefPtr<Frame>;

// Represents a single render pass on a command queue. There may be multiple
// frames issuing commands per render draw call. Frame is passed into a
// Renderer, which uses it to obtain command buffers, submit partial frames, do
// profiling, etc.
class Frame : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  ~Frame();

  // Submit the current CommandBuffer, and obtain a new CommandBuffer for subsequent commands.  If
  // |partial_frame_done| is not null, it will be signaled when the submitted CommandBuffer is
  // finished.
  void SubmitPartialFrame(const SemaphorePtr& partial_frame_done);

  // Submit the frame's final CommandBuffer.  When it is finished, |frame_done| will be signaled and
  // |frame_retired_callback| will be invoked; the latter occurs when the command-buffer is cleaned
  // up in Escher::Cleanup(), perhaps more than a millisecond later.
  void EndFrame(const SemaphorePtr& frame_done, FrameRetiredCallback frame_retired_callback);

  // Submit the frame's final CommandBuffer.  When it is finished, all of the semaphores in the
  // vector |semaphores| will signaled and |frame_retired_callback| will be invoked; the latter
  // occurs when the the command-buffer is cleaned up in Escher::Cleanup(), perhaps more than a
  // millisecond later.
  void EndFrame(const std::vector<SemaphorePtr>& semaphores,
                FrameRetiredCallback frame_retired_callback);

  // If profiling is enabled, inserts a Vulkan timestamp query into the frame's
  // current CommandBuffer; the result will be inserted into the trace log.
  // |stages| denotes the set of pipeline stages that must be completed by all
  // previously-issued commands; see TimestampProfiler docs for more details.
  void AddTimestamp(const char* name,
                    vk::PipelineStageFlagBits stages = vk::PipelineStageFlagBits::eBottomOfPipe);

  // See |CommandBuffer::DisableLazyPipelineCreation()|.  Disables lazy pipeline creation on the
  // frame's current and subsequent CommandBuffers.
  void DisableLazyPipelineCreation();

  uint64_t frame_number() const { return frame_number_; }

  CommandBuffer* cmds() const { return command_buffer_.get(); }
  vk::CommandBuffer vk_command_buffer() const;
  uint64_t command_buffer_sequence_number() const { return command_buffer_sequence_number_; }
  GpuAllocator* gpu_allocator();
  BlockAllocator* host_allocator() { return &block_allocator_; }

  // Allocate temporary CPU memory that is valid until EndFrame() is called.
  template <typename T>
  T* Allocate() {
    return block_allocator_.Allocate<T>();
  }

  // Allocate temporary CPU memory that is valid until EndFrame() is called.
  template <typename T>
  T* AllocateMany(size_t count) {
    return block_allocator_.AllocateMany<T>(count);
  }

  // Allocate temporary GPU uniform buffer memory that is valid until the frame is finished
  // rendering (after EndFrame() is called).
  UniformAllocation AllocateUniform(size_t size, size_t alignment) {
    return uniform_block_allocator_.Allocate(size, alignment);
  }

  bool use_protected_memory() const { return use_protected_memory_; }

 private:
  // These resources will be retained until the current frame is finished
  // running on the GPU.
  void KeepAlive(ResourcePtr resource);

  // Constructor called by Escher::NewFrame().
  // NOTE: moving the BlockAllocator into the Frame (instead of e.g. passing a
  // unique_ptr) avoids an extra pointer indirection on each allocation.
  friend class impl::FrameManager;
  Frame(impl::FrameManager* manager, CommandBuffer::Type requested_type, BlockAllocator allocator,
        impl::UniformBufferPoolWeakPtr uniform_buffer_pool, uint64_t frame_number,
        const char* trace_literal, const char* gpu_vthread_literal, uint64_t gpu_vthread_id,
        bool enable_gpu_logging, bool use_protected_memory);
  void BeginFrame();

  // Issues a new CommandBuffer for a frame, and marks the frame as InProgress.
  void IssueCommandBuffer();

  // Called by impl::FrameManager when the Frame is returned to the pool, so
  // that it can be reused in newly constructed frames.
  BlockAllocator TakeBlockAllocator() { return std::move(block_allocator_); }

  // Called by BatchGpuUploader and BatchGpuDownloader to write to the
  // new_command_buffer_ and gather work to post to the GPU.
  // TODO(fxbug.dev/24063) Remove these functions once BatchGpuUploader::Writers are
  // backed by secondary buffers, and the frame's primary command buffer is not
  // moved into the Writer.
  friend class BatchGpuUploader;
  friend class BatchGpuDownloader;
  CommandBufferPtr TakeCommandBuffer();
  void PutCommandBuffer(CommandBufferPtr command_buffer);

  enum class State {
    kReadyToBegin,
    kInProgress,
    kFinishing,
  };

  State state_ = State::kReadyToBegin;

  // The frame number associated with this frame. Used to correlate work across
  // threads for tracing events.
  const uint64_t frame_number_;
  // A unique number to identify this escher frame. It can diverge from
  // frame_number_, as frame_number_ is used by the client for its own tracking.
  const uint64_t escher_frame_number_;
  // A string constant that is the name of the trace event this frame will
  // generate.
  const char* trace_literal_;
  // A string constant that is the name of the virtual thread this frame
  // generates events for.
  const char* gpu_vthread_literal_;
  // A unique identifier for the virtual thread this frame generates events for.
  const uint64_t gpu_vthread_id_;
  bool enable_gpu_logging_;
  bool use_protected_memory_;
  vk::Queue queue_;

  CommandBuffer::Type command_buffer_type_;
  // The sequence number of the command_buffer managed by this frame. Cached
  // here to track which command_buffer was managed by this frame if the command
  // buffer was taken (via TakeCommandBuffer()) for GPU uploads.
  uint64_t command_buffer_sequence_number_;
  CommandBufferPtr command_buffer_;

  BlockAllocator block_allocator_;

  // TODO(42570): investigate whether this memory is host-coherent, and whether it should be
  // (it seems like it isn't and should be).  Document the usage guarantees/requirements in
  // AllocateUniform(), above.
  UniformBlockAllocator uniform_block_allocator_;

  TimestampProfilerPtr profiler_;
  uint32_t submission_count_ = 0;

  // TODO(fxbug.dev/7194): ideally we can move away from explicitly retaining used
  // resources in the Frame.  For now, this approach is easy and relatively
  // fool-proof.
  std::vector<ResourcePtr> keep_alive_;

  bool disable_lazy_pipeline_creation_ = false;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_FRAME_H_
