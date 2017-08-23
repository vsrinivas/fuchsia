// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/renderer/timestamper.h"
#include "escher/vk/vulkan_context.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace escher {

typedef std::function<void()> FrameRetiredCallback;

class Renderer : public ftl::RefCountedThreadSafe<Renderer>,
                 public Timestamper {
 public:
  void RunOffscreenBenchmark(
      uint32_t framebuffer_width,
      uint32_t framebuffer_height,
      vk::Format framebuffer_format,
      size_t frame_count,
      std::function<void(const ImagePtr&, const SemaphorePtr&)> draw_func);

  const VulkanContext& vulkan_context() { return context_; }

  void set_enable_profiling(bool enabled) { enable_profiling_ = enabled; }

  Escher* escher() const { return escher_; }

  uint64_t frame_number() const { return frame_number_; }

 protected:
  explicit Renderer(Escher* escher);
  virtual ~Renderer();

  impl::EscherImpl* escher_impl() const;

  // Obtain a CommandBuffer, to record commands for the current frame.
  void BeginFrame();
  void SubmitPartialFrame();
  void EndFrame(const SemaphorePtr& frame_done,
                FrameRetiredCallback frame_retired_callback);

  // If profiling is enabled, then when the current frame is completed, all
  // timestamps from this frame will be printed out.
  void AddTimestamp(const char* name) override;

  impl::CommandBuffer* current_frame() { return current_frame_; }

  const VulkanContext context_;

 private:
  Escher* const escher_;
  impl::CommandBufferPool* pool_;
  impl::CommandBuffer* current_frame_ = nullptr;

  uint64_t frame_number_ = 0;

  bool enable_profiling_ = false;
  // Created in BeginFrame() when profiling is enabled.
  TimestampProfilerPtr profiler_;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FTL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher
