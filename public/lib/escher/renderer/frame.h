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

namespace escher {

typedef std::function<void()> FrameRetiredCallback;

class Frame;
using FramePtr = fxl::RefPtr<Frame>;

// Represents a single rendered frame.  Passed into a Renderer, which uses it to
// obtain command buffers, submit partial frames, do profiling, etc.
class Frame : public Reffable {
 public:
  ~Frame();

  // Obtain a CommandBuffer, to record commands for the current frame.
  void SubmitPartialFrame(const SemaphorePtr& partial_frame_done);
  void EndFrame(const SemaphorePtr& frame_done,
                FrameRetiredCallback frame_retired_callback);

  void AddTimestamp(const char* name);

  Escher* escher() const { return escher_; }
  uint64_t frame_number() const { return frame_number_; }
  impl::CommandBuffer* command_buffer() const { return command_buffer_; }
  vk::CommandBuffer vk_command_buffer() const { return vk_command_buffer_; }
  GpuAllocator* gpu_allocator();

 private:
  // Constructor called by Escher::NewFrame().
  friend class Escher;
  Frame(Escher* escher,
        uint64_t frame_number,
        const char* trace_literal,
        bool enable_gpu_logging);
  void BeginFrame();

  static void LogGpuQueryResults(
      uint64_t frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps);

  static void TraceGpuQueryResults(
      uint64_t frame_number,
      const std::vector<TimestampProfiler::Result>& timestamps,
      const char* trace_literal);

  Escher* const escher_;
  const uint64_t frame_number_;
  const char* trace_literal_;
  bool enable_gpu_logging_;
  vk::Queue queue_;
  impl::CommandBufferPool* pool_;
  impl::CommandBuffer* command_buffer_ = nullptr;
  vk::CommandBuffer vk_command_buffer_;

  TimestampProfilerPtr profiler_;
  uint32_t submission_count_ = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_FRAME_H_
