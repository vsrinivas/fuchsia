// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/tests/util.h"

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"

namespace lib_ui_input_tests {

using ScenicEvent = fuchsia::ui::scenic::Event;
using escher::impl::CommandBufferSequencer;
using fuchsia::ui::input::Command;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendPointerInputCmd;
using fuchsia::ui::scenic::SessionListener;
using scenic::ResourceId;
using scenic_impl::Scenic;
using scenic_impl::gfx::DisplayManager;
using scenic_impl::gfx::test::GfxSystemForTest;
using scenic_impl::input::InputSystem;
using scenic_impl::test::ScenicTest;

void CreateTokenPair(zx::eventpair* t1, zx::eventpair* t2) {
  zx_status_t status = zx::eventpair::create(/*flags*/ 0u, t1, t2);
  FXL_CHECK(status == ZX_OK);
}

void InputSystemTest::RequestToPresent(scenic::Session* session) {
  bool scene_presented = false;
  session->Present(
      /*presentation time*/ 0,
      [&scene_presented](fuchsia::images::PresentationInfo info) {
        scene_presented = true;
      });
  RunLoopFor(zx::msec(20));  // Schedule the render task.
  EXPECT_TRUE(scene_presented);
}

void InputSystemTest::TearDown() {
  // A clean teardown sequence is a little involved but possible.
  // 0. Sessions Flush their last resource-release cmds (e.g., ~SessionWrapper).
  // 1. Scenic runs the last resource-release cmds.
  RunLoopUntilIdle();
  // 2. Destroy Scenic before destroying the command buffer sequencer (CBS).
  //    This ensures no CBS listeners are active by the time CBS is destroyed.
  //    Scenic is destroyed by the superclass TearDown (now), CBS is destroyed
  //    by the implicit class destructor (later).
  ScenicTest::TearDown();
}

void InputSystemTest::InitializeScenic(Scenic* scenic) {
  auto display_manager = std::make_unique<DisplayManager>();
  display_manager->SetDefaultDisplayForTests(std::make_unique<TestDisplay>(
      /*id*/ 0, test_display_width_px(), test_display_height_px()));
  command_buffer_sequencer_ = std::make_unique<CommandBufferSequencer>();
  gfx_ = scenic->RegisterSystem<GfxSystemForTest>(
      std::move(display_manager), command_buffer_sequencer_.get());
  input_ = scenic->RegisterSystem<InputSystem>(gfx_);
}

SessionWrapper::SessionWrapper(Scenic* scenic) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<SessionListener> listener_handle;
  fidl::InterfaceRequest<SessionListener> listener_request =
      listener_handle.NewRequest();
  scenic->CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  session_ = std::make_unique<scenic::Session>(std::move(session_ptr),
                                               std::move(listener_request));
  root_node_ = std::make_unique<scenic::EntityNode>(session_.get());

  session_->set_event_handler([this](fidl::VectorPtr<ScenicEvent> events) {
    for (ScenicEvent& event : *events) {
      if (event.is_input()) {
        events_.push_back(std::move(event.input()));
      }
      // Ignore other event types for this test.
    }
  });
}

SessionWrapper::~SessionWrapper() {
  root_node_.reset();  // Let go of the resource; enqueue the release cmd.
  session_->Flush();   // Ensure Scenic receives the release cmd.
}

void SessionWrapper::RunNow(
    fit::function<void(scenic::Session* session, scenic::EntityNode* root_node)>
        create_scene_callback) {
  create_scene_callback(session_.get(), root_node_.get());
}

void SessionWrapper::ExamineEvents(
    fit::function<void(const std::vector<InputEvent>& events)>
        examine_events_callback) {
  examine_events_callback(events_);
}

PointerEventGenerator::PointerEventGenerator(ResourceId compositor_id,
                                             uint32_t device_id,
                                             uint32_t pointer_id,
                                             PointerEventType type) {
  compositor_id_ = compositor_id;
  blank_.device_id = device_id;
  blank_.pointer_id = pointer_id;
  blank_.type = type;
}

fuchsia::ui::input::Command PointerEventGenerator::Add(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::ADD;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Down(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::DOWN;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Move(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::MOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Up(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::UP;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Remove(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::REMOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::MakeInputCommand(
    PointerEvent event) {
  SendPointerInputCmd pointer_cmd;
  pointer_cmd.compositor_id = compositor_id_;
  pointer_cmd.pointer_event = std::move(event);

  Command input_cmd;
  input_cmd.set_send_pointer_input(std::move(pointer_cmd));

  return input_cmd;
}

}  // namespace lib_ui_input_tests

