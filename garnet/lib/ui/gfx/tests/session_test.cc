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

void SessionTest::SetUp() {
  display_manager_ = std::make_unique<DisplayManager>();
  display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 0, /*px-height*/ 0));
  frame_scheduler_ = std::make_unique<DefaultFrameScheduler>(
      display_manager_->default_display(),
      std::make_unique<FramePredictor>(DefaultFrameScheduler::kInitialRenderDuration,
                                       DefaultFrameScheduler::kInitialUpdateDuration));

  session_context_ = CreateSessionContext();
  session_ = CreateSession();
}

void SessionTest::TearDown() {
  session_.reset();
  frame_scheduler_.reset();
  display_manager_.reset();
  events_.clear();
}

SessionContext SessionTest::CreateSessionContext() {
  FXL_DCHECK(frame_scheduler_);
  FXL_DCHECK(display_manager_);

  SessionContext session_context{
      vk::Device(),
      nullptr,                 // escher::Escher*
      nullptr,                 // escher::ResourceRecycler
      nullptr,                 // escher::ImageFactory*
      nullptr,                 // escher::RoundedRectFactory*
      nullptr,                 // escher::ReleaseFenceSignaller*
      nullptr,                 // EventTimestamper*
      frame_scheduler_.get(),  // FrameScheduler*
      display_manager_.get(),  // DisplayManager*
      SceneGraphWeakPtr(),     // SceneGraphWeakPtr
      nullptr,                 // ResourceLinker*
      nullptr                  // ViewLinker*
  };
  return session_context;
}

CommandContext SessionTest::CreateCommandContext() {
  // By default, return the empty command context.
  return CommandContext(nullptr);
}

std::unique_ptr<Session> SessionTest::CreateSession() {
  static uint32_t next_id = 1;
  return std::make_unique<Session>(next_id++, session_context_, this, error_reporter());
}

bool SessionTest::Apply(::fuchsia::ui::gfx::Command command) {
  auto command_context = CreateCommandContext();
  auto retval = session_->ApplyCommand(&command_context, std::move(command));
  command_context.Flush();
  return retval;
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
