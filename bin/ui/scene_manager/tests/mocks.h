// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/displays/display_manager.h"
#include "garnet/bin/ui/scene_manager/engine/engine.h"
#include "garnet/bin/ui/scene_manager/engine/session_handler.h"
#include "garnet/bin/ui/scene_manager/release_fence_signaller.h"
#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"

namespace scene_manager {
namespace test {

// Subclass SessionHandler to make testing easier.
class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(
      Engine* engine,
      SessionId session_id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // scenic::Session interface methods.
  void Enqueue(::fidl::Array<scenic::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               ::fidl::Array<mx::event> acquire_fences,
               ::fidl::Array<mx::event> release_fences,
               const PresentCallback& callback) override;

  // Return the number of Enqueue()/Present()/Connect() messages that have
  // been processed.
  uint32_t enqueue_count() const { return enqueue_count_; }
  uint32_t present_count() const { return present_count_; }

 private:
  std::atomic<uint32_t> enqueue_count_;
  std::atomic<uint32_t> present_count_;
};

class ReleaseFenceSignallerForTest : public ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(mx::event fence) override;

  uint32_t num_calls_to_add_cpu_release_fence() {
    return num_calls_to_add_cpu_release_fence_;
  }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class EngineForTest : public Engine {
 public:
  EngineForTest(DisplayManager* display_manager,
                std::unique_ptr<ReleaseFenceSignaller> r);
  using Engine::FindSession;

 private:
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener) override;
};

}  // namespace test
}  // namespace scene_manager
