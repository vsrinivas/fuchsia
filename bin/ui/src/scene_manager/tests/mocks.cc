// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/tests/mocks.h"

namespace mozart {
namespace scene {
namespace test {

SessionHandlerForTest::SessionHandlerForTest(
    SessionContext* session_context,
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener)
    : SessionHandler(session_context,
                     session_id,
                     std::move(request),
                     std::move(listener)),
      enqueue_count_(0),
      present_count_(0),
      connect_count_(0) {}

void SessionHandlerForTest::Enqueue(::fidl::Array<mozart2::OpPtr> ops) {
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

void SessionHandlerForTest::Connect(
    ::fidl::InterfaceRequest<mozart2::Session> session,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  SessionHandler::Connect(std::move(session), std::move(listener));
  ++connect_count_;
}

SceneManagerImplForTest::SceneManagerImplForTest(
    Display* display,
    std::unique_ptr<SessionContext> session_context)
    : SceneManagerImpl(display, std::move(session_context), nullptr) {}

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(mx::event fence) {
  num_calls_to_add_cpu_release_fence_++;
  // Signal immediately for testing purposes.
  fence.signal(0u, kFenceSignalled);
}

SessionContextForTest::SessionContextForTest(
    std::unique_ptr<ReleaseFenceSignaller> r)
    : SessionContext(std::move(r)) {}

std::unique_ptr<SessionHandler> SessionContextForTest::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  return std::make_unique<SessionHandlerForTest>(
      this, session_id, std::move(request), std::move(listener));
}

}  // namespace test
}  // namespace scene
}  // namespace mozart
