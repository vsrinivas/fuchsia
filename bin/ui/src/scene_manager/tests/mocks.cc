// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/tests/mocks.h"

namespace scene_manager {
namespace test {

SessionHandlerForTest::SessionHandlerForTest(
    Engine* engine,
    SessionId session_id,
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener)
    : SessionHandler(engine,
                     session_id,
                     std::move(request),
                     std::move(listener)),
      enqueue_count_(0),
      present_count_(0) {}

void SessionHandlerForTest::Enqueue(::fidl::Array<scenic::OpPtr> ops) {
  SessionHandler::Enqueue(std::move(ops));
  ++enqueue_count_;
}

void SessionHandlerForTest::Present(uint64_t presentation_time,
                                    ::fidl::Array<mx::event> acquire_fences,
                                    ::fidl::Array<mx::event> release_fences,
                                    const PresentCallback& callback) {
  SessionHandler::Present(presentation_time, std::move(acquire_fences),
                          std::move(release_fences), callback);
  ++present_count_;
}

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(mx::event fence) {
  num_calls_to_add_cpu_release_fence_++;
  // Signal immediately for testing purposes.
  fence.signal(0u, kFenceSignalled);
}

EngineForTest::EngineForTest(DisplayManager* display_manager,
                             std::unique_ptr<ReleaseFenceSignaller> r)
    : Engine(display_manager, std::move(r)) {}

std::unique_ptr<SessionHandler> EngineForTest::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener) {
  return std::make_unique<SessionHandlerForTest>(
      this, session_id, std::move(request), std::move(listener));
}

}  // namespace test
}  // namespace scene_manager
