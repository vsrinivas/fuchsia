// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_FRAME_H_
#define LIB_ESCHER_RENDERER_FRAME_H_

#include <cstdint>
#include <functional>
#include <vulkan/vulkan.hpp>

#include "lib/escher/base/reffable.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/renderer/uniform_block_allocator.h"
#include "lib/escher/util/block_allocator.h"
#include "lib/escher/vk/command_buffer.h"

namespace escher {

typedef std::function<void()> FrameRetiredCallback;

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

  // Obtain a CommandBuffer, to record commands for the current frame.
  void SubmitPartialFrame(const SemaphorePtr& partial_frame_done);
  void EndFrame(const SemaphorePtr& frame_done,
                FrameRetiredCallback frame_retired_callback);

  // If profiling is enabled, inserts a Vulkan timestamp query into the frame's
  // current CommandBuffer; the result will be inserted into the trace log.
  // |stages| denotes the set of pipeline stages that must be completed by all
  // previously-issued commands; see TimestampProfiler docs for more details.
  void AddTimestamp(const char* name,
                    vk::PipelineStageFlagBits stages =
                        vk::PipelineStageFlagBits::eBottomOfPipe);

  uint64_t frame_number() const { return frame_number_; }

  CommandBuffer* cmds() const { return command_buffer_.get(); }
  impl::CommandBuffer* command_buffer() const;
  vk::CommandBuffer vk_command_buffer() const;
  uint64_t command_buffer_sequence_number() const {
    return command_buffer_sequence_number_;
  }
  GpuAllocator* gpu_allocator();

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

  // Allocate temporary GPU uniform buffer memory that is value until the frame
  // is finished rendering (after EndFrame() is called).
  UniformAllocation AllocateUniform(size_t size, size_t alignment) {
    return uniform_block_allocator_.Allocate(size, alignment);
  }

 private:
  // These resources will be retained until the current frame is finished
  // running on the GPU.
  void KeepAlive(ResourcePtr resource);

  // Constructor called by Escher::NewFrame().
  // NOTE: moving the BlockAllocator into the Frame (instead of e.g. passing a
  // unique_ptr) avoids an extra pointer indirection on each allocation.
  friend class impl::FrameManager;
  Frame(impl::FrameManager* manager, CommandBuffer::Type requested_type,
        BlockAllocator allocator,
        impl::UniformBufferPoolWeakPtr uniform_buffer_pool,
        uint64_t frame_number, const char* trace_literal,
        bool enable_gpu_logging);
  void BeginFrame();

  // Issues a new CommandBuffer for a frame, and marks the frame as InProgress.
  void IssueCommandBuffer();

  // Called by impl::FrameManager when the Frame is returned to the pool, so
  // that it can be reused in newly constructed frames.
  BlockAllocator TakeBlockAllocator() { return std::move(block_allocator_); }

  // Called by BatchGpuUploader to write to the new_command_buffer_ and gather
  // work to post to the GPU.
  // TODO(SCN-846) Remove these functions once BatchGpuUploader::Writers are
  // backed by secondary buffers, and the frame's primary command buffer is not
  // moved into the Writer.
  friend class BatchGpuUploader;
  CommandBufferPtr TakeCommandBuffer();
  void PutCommandBuffer(CommandBufferPtr command_buffer);

  static void LogGpuQueryResults(
      uint64_t escher_frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps);

  static void TraceGpuQueryResults(
      uint64_t frame_number, uint64_t escher_frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps,
      const char* trace_literal);

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
  const char* trace_literal_;
  bool enable_gpu_logging_;
  vk::Queue queue_;

  CommandBuffer::Type command_buffer_type_;
  // The sequence number of the command_buffer managed by this frame. Cached
  // here to track which command_buffer was managed by this frame if the command
  // buffer was taken (via TakeCommandBuffer()) for GPU uploads.
  uint64_t command_buffer_sequence_number_;
  CommandBufferPtr command_buffer_;

  BlockAllocator block_allocator_;

  UniformBlockAllocator uniform_block_allocator_;

  TimestampProfilerPtr profiler_;
  uint32_t submission_count_ = 0;

  // TODO(ES-103): ideally we can move away from explicitly retaining used
  // resources in the Frame.  For now, this approach is easy and relatively
  // fool-proof.
  std::vector<ResourcePtr> keep_alive_;
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_FRAME_H_
