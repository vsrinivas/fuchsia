// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void SessionTest::SetUp() { session_ = CreateSession(); }

void SessionTest::TearDown() {
  session_.reset();
  session_manager_.reset();
  frame_scheduler_.reset();
  display_manager_.reset();
  events_.clear();
}

SessionContext SessionTest::CreateBarebonesSessionContext() {
  session_manager_ = std::make_unique<SessionManager>();

  display_manager_ = std::make_unique<DisplayManager>();
  display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 0, /*px-height*/ 0));
  frame_scheduler_ = std::make_unique<DefaultFrameScheduler>(
      display_manager_->default_display());
  SessionContext session_context{
      vk::Device(),
      nullptr,                 // escher::Escher*
      nullptr,                 // escher::ResourceRecycler
      nullptr,                 // escher::ImageFactory*
      nullptr,                 // escher::RoundedRectFactory*
      nullptr,                 // escher::ReleaseFenceSignaller*
      nullptr,                 // EventTimestamper*
      session_manager_.get(),  // SessionManager*
      frame_scheduler_.get(),  // FrameScheduler*
      display_manager_.get(),  // DisplayManager*
      SceneGraphWeakPtr(),     // SceneGraphWeakPtr
      nullptr,                 // ResourceLinker*
      nullptr                  // ViewLinker*
  };
  return session_context;
}

std::unique_ptr<SessionForTest> SessionTest::CreateSession() {
  return std::make_unique<SessionForTest>(1, CreateBarebonesSessionContext(),
                                          this, error_reporter());
}

void SessionTest::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void SessionTest::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void SessionTest::EnqueueEvent(fuchsia::ui::scenic::Command unhandled) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(unhandled));
  events_.push_back(std::move(scenic_event));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
