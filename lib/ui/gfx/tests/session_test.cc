// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"

#include "garnet/lib/ui/gfx/tests/mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

FakeUpdateScheduler::FakeUpdateScheduler(SessionManager* session_manager)
    : session_manager_(session_manager) {}

void FakeUpdateScheduler::ScheduleUpdate(uint64_t presentation_time) {
  CommandContext empty_command_context(nullptr);
  session_manager_->ApplyScheduledSessionUpdates(&empty_command_context,
                                                 presentation_time, 0);
}

void SessionTest::SetUp() { session_ = CreateSession(); }

void SessionTest::TearDown() {
  session_->TearDown();
  session_ = nullptr;
  events_.clear();
}

SessionContext SessionTest::CreateBarebonesSessionContext() {
  session_manager_ = std::make_unique<SessionManager>();
  update_scheduler_ =
      std::make_unique<FakeUpdateScheduler>(session_manager_.get());
  SessionContext session_context{
      vk::Device(),
      nullptr,                  // escher::Escher*
      0,                        // imported_memory_type_index;
      nullptr,                  // escher::ResourceRecycler
      nullptr,                  // escher::ImageFactory*
      nullptr,                  // escher::impl::GpuUploader*
      nullptr,                  // escher::RoundedRectFactory*
      nullptr,                  // escher::ReleaseFenceSignaller*
      nullptr,                  // EventTimestamper*
      session_manager_.get(),   // SessionManager*
      nullptr,                  // FrameScheduler*
      update_scheduler_.get(),  // UpdateScheduler*
      nullptr,                  // DisplayManager*
      SceneGraphWeakPtr(),      // SceneGraphWeakPtr
      nullptr,                  // ResourceLinker*
      nullptr                   // ViewLinker*
  };
  return session_context;
}

fxl::RefPtr<SessionForTest> SessionTest::CreateSession() {
  return fxl::MakeRefCounted<SessionForTest>(1, CreateBarebonesSessionContext(),
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
