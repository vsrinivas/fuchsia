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
#include "lib/escher/util/block_allocator.h"

namespace escher {

typedef std::function<void()> FrameRetiredCallback;

class Frame;
using FramePtr = fxl::RefPtr<Frame>;

// Represents a single rendered frame.  Passed into a Renderer, which uses it to
// obtain command buffers, submit partial frames, do profiling, etc.
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

  CommandBuffer* cmds() const { return new_command_buffer_.get(); }
  impl::CommandBuffer* command_buffer() const { return command_buffer_; }
  vk::CommandBuffer vk_command_buffer() const { return vk_command_buffer_; }
  GpuAllocator* gpu_allocator();

  template <typename T>
  T* Allocate() {
    return block_allocator_.Allocate<T>();
  }

  template <typename T>
  T* AllocateMany(size_t count) {
    return block_allocator_.AllocateMany<T>(count);
  }

 private:
  // Constructor called by Escher::NewFrame().
  // NOTE: moving the BlockAllocator into the Frame (instead of e.g. passing a
  // unique_ptr) avoids an extra pointer indirection on each allocation.
  friend class impl::FrameManager;
  Frame(impl::FrameManager* manager, BlockAllocator allocator,
        uint64_t frame_number, const char* trace_literal,
        bool enable_gpu_logging);
  void BeginFrame();

  // Called by impl::FrameManager when the Frame is returned to the pool, so
  // that it can be reused in newly constructed frames.
  BlockAllocator TakeBlockAllocator() { return std::move(block_allocator_); }

  static void LogGpuQueryResults(
      uint64_t frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps);

  static void TraceGpuQueryResults(
      uint64_t frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps,
      const char* trace_literal);

  // The frame number associated with this frame. Used to correlate work across
  // threads for tracing events.
  const uint64_t frame_number_;
  // A unique number to identify this escher frame. It can diverge from
  // frame_number_, as frame_number_ is used by the client for its own tracking.
  const uint64_t escher_frame_number_;
  const char* trace_literal_;
  bool enable_gpu_logging_;
  vk::Queue queue_;

  CommandBufferPtr new_command_buffer_;
  impl::CommandBuffer* command_buffer_ = nullptr;
  vk::CommandBuffer vk_command_buffer_;

  BlockAllocator block_allocator_;

  TimestampProfilerPtr profiler_;
  uint32_t submission_count_ = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_FRAME_H_
