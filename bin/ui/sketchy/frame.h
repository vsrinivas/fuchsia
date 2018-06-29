// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_FRAME_H_
#define GARNET_BIN_UI_SKETCHY_FRAME_H_

#include <lib/zx/event.h>

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/cpp/session.h"

namespace sketchy_service {

class SharedBufferPool;

// Manages the compute commands of a Canvas::Present() request, and its
// corresponding synchronization semantics.
class Frame final {
 public:
  explicit Frame(SharedBufferPool* shared_buffer_pool,
                 bool enable_profiler = false);

  zx::event DuplicateReleaseFence();
  void RequestScenicPresent(scenic::Session* session,
                            uint64_t presentation_time,
                            scenic::Session::PresentCallback callback);

  SharedBufferPool* shared_buffer_pool() const { return shared_buffer_pool_; }
  escher::impl::CommandBuffer* command() const { return command_; }
  escher::TimestampProfiler* profiler() const { return profiler_.get(); }
  bool init_failed() const { return init_failed_; }

 private:
  SharedBufferPool* shared_buffer_pool_;
  escher::Escher* escher_;
  escher::impl::CommandBuffer* command_;
  escher::TimestampProfilerPtr profiler_;
  escher::SemaphorePtr acquire_semaphore_;
  zx::event acquire_fence_;
  zx::event release_fence_;
  bool init_failed_ = false;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_FRAME_H_
