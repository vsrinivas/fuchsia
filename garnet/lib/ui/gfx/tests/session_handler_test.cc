// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_handler_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void SessionHandlerTest::SetUp() {
  InitializeScenic();
  InitializeDisplayManager();
  InitializeEngine();

  InitializeSessionHandler();
}

void SessionHandlerTest::TearDown() {
  command_dispatcher_.reset();
  engine_.reset();
  command_buffer_sequencer_.reset();
  display_manager_.reset();
  scenic_.reset();
  app_context_.reset();
  events_.clear();
}

void SessionHandlerTest::InitializeScenic() {
  // TODO(SCN-720): Wrap Create using ::gtest::Environment
  // instead of this hack.  This code has the chance to break non-ScenicTests.
  app_context_ = sys::ComponentContext::Create();
  scenic_ = std::make_unique<Scenic>(app_context_.get(), [] {});
}

void SessionHandlerTest::InitializeSessionHandler() {
  auto session_context = engine_->session_context();
  auto session_manager = session_context.session_manager;
  auto session_id = SessionId(1);

  InitializeScenicSession(session_id);
  command_dispatcher_ = session_manager->CreateCommandDispatcher(
      CommandDispatcherContext(scenic_.get(), scenic_session_.get()),
      std::move(session_context));
}

void SessionHandlerTest::InitializeDisplayManager() {
  display_manager_ = std::make_unique<DisplayManager>();
  display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 0, /*px-height*/ 0));
}

void SessionHandlerTest::InitializeEngine() {
  command_buffer_sequencer_ =
      std::make_unique<escher::impl::CommandBufferSequencer>();

  auto mock_release_fence_signaller =
      std::make_unique<ReleaseFenceSignallerForTest>(
          command_buffer_sequencer_.get());

  engine_ = std::make_unique<EngineForTest>(
      app_context_.get(), display_manager_.get(),
      std::move(mock_release_fence_signaller),
      /*event reporter*/ this, this->error_reporter());
}

void SessionHandlerTest::InitializeScenicSession(SessionId session_id) {
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener;
  scenic_session_ =
      std::make_unique<scenic_impl::Session>(session_id, std::move(listener));
}

void SessionHandlerTest::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void SessionHandlerTest::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void SessionHandlerTest::EnqueueEvent(fuchsia::ui::scenic::Command unhandled) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(unhandled));
  events_.push_back(std::move(scenic_event));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
