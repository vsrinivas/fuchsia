// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/client/session.h"
#include "zx/event.h"

namespace sketchy_service {

// Manages the compute commands of a Canvas::Present() request, and its
// corresponding synchronization semantics.
class Frame final {
 public:
  explicit Frame(escher::BufferFactory* buffer_factory,
                 bool enable_profiler = false);

  zx::event DuplicateReleaseFence();
  void RequestScenicPresent(
      scenic_lib::Session* session,
      uint64_t presentation_time,
      const scenic_lib::Session::PresentCallback& callback);

  escher::impl::CommandBuffer* command() const { return command_; }
  escher::BufferFactory* buffer_factory() const { return buffer_factory_; }
  escher::SemaphorePtr acquire_semaphore() const { return acquire_semaphore_; }
  escher::TimestampProfiler* profiler() const { return profiler_.get(); }
  bool success() const { return success_; }

 private:
  escher::BufferFactory* buffer_factory_;
  escher::Escher* escher_;
  escher::impl::CommandBuffer* command_;
  escher::TimestampProfilerPtr profiler_;
  escher::SemaphorePtr acquire_semaphore_;
  zx::event acquire_fence_;
  zx::event release_fence_;
  bool success_;
};

}  // namespace sketchy_service
