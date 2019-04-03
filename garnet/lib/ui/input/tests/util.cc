// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/tests/util.h"

#include <hid/hid.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "src/lib/fxl/logging.h"

namespace lib_ui_input_tests {

using InputCommand = fuchsia::ui::input::Command;
using ScenicEvent = fuchsia::ui::scenic::Event;
using escher::impl::CommandBufferSequencer;
using fuchsia::ui::input::InputEvent;
using fuchsia::ui::input::KeyboardEvent;
using fuchsia::ui::input::KeyboardEventPhase;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendKeyboardInputCmd;
using fuchsia::ui::input::SendPointerInputCmd;
using fuchsia::ui::scenic::SessionListener;
using scenic_impl::ResourceId;
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
  session->Present(/*presentation time*/ 0, [](auto) {});
  RunLoopFor(zx::msec(20));  // Schedule the render task.
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

  session_->set_event_handler([this](std::vector<ScenicEvent> events) {
    for (ScenicEvent& event : events) {
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

PointerCommandGenerator::PointerCommandGenerator(ResourceId compositor_id,
                                                 uint32_t device_id,
                                                 uint32_t pointer_id,
                                                 PointerEventType type)
    : compositor_id_(compositor_id) {
  blank_.device_id = device_id;
  blank_.pointer_id = pointer_id;
  blank_.type = type;
}

InputCommand PointerCommandGenerator::Add(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::ADD;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Down(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::DOWN;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Move(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::MOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Up(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::UP;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Remove(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::REMOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::MakeInputCommand(PointerEvent event) {
  SendPointerInputCmd pointer_cmd;
  pointer_cmd.compositor_id = compositor_id_;
  pointer_cmd.pointer_event = std::move(event);

  InputCommand input_cmd;
  input_cmd.set_send_pointer_input(std::move(pointer_cmd));

  return input_cmd;
}

KeyboardCommandGenerator::KeyboardCommandGenerator(ResourceId compositor_id,
                                                   uint32_t device_id)
    : compositor_id_(compositor_id) {
  blank_.device_id = device_id;
}

InputCommand KeyboardCommandGenerator::Pressed(uint32_t hid_usage,
                                               uint32_t modifiers) {
  KeyboardEvent event;
  fidl::Clone(blank_, &event);
  event.phase = KeyboardEventPhase::PRESSED;
  event.hid_usage = hid_usage;
  event.modifiers = modifiers;
  return MakeInputCommand(event);
}

InputCommand KeyboardCommandGenerator::Released(uint32_t hid_usage,
                                                uint32_t modifiers) {
  KeyboardEvent event;
  fidl::Clone(blank_, &event);
  event.phase = KeyboardEventPhase::RELEASED;
  event.hid_usage = hid_usage;
  event.modifiers = modifiers;
  return MakeInputCommand(event);
}

InputCommand KeyboardCommandGenerator::Cancelled(uint32_t hid_usage,
                                                 uint32_t modifiers) {
  KeyboardEvent event;
  fidl::Clone(blank_, &event);
  event.phase = KeyboardEventPhase::CANCELLED;
  event.hid_usage = hid_usage;
  event.modifiers = modifiers;
  return MakeInputCommand(event);
}

InputCommand KeyboardCommandGenerator::Repeat(uint32_t hid_usage,
                                              uint32_t modifiers) {
  KeyboardEvent event;
  fidl::Clone(blank_, &event);
  event.phase = KeyboardEventPhase::REPEAT;
  event.hid_usage = hid_usage;
  event.modifiers = modifiers;
  return MakeInputCommand(event);
}

InputCommand KeyboardCommandGenerator::MakeInputCommand(KeyboardEvent event) {
  // Typically code point is inferred this same way by DeviceState.
  event.code_point =
      hid_map_key(event.hid_usage,
                  event.modifiers & (fuchsia::ui::input::kModifierShift |
                                     fuchsia::ui::input::kModifierCapsLock),
                  qwerty_map);

  SendKeyboardInputCmd keyboard_cmd;
  keyboard_cmd.compositor_id = compositor_id_;
  keyboard_cmd.keyboard_event = std::move(event);

  InputCommand input_cmd;
  input_cmd.set_send_keyboard_input(std::move(keyboard_cmd));

  return input_cmd;
}

bool PointerMatches(const PointerEvent& event, uint32_t pointer_id,
                    PointerEventPhase phase, float x, float y) {
  using fuchsia::ui::input::operator<<;

  if (event.pointer_id != pointer_id) {
    FXL_LOG(ERROR) << "  Actual: " << event.pointer_id;
    FXL_LOG(ERROR) << "Expected: " << pointer_id;
    return false;
  } else if (event.phase != phase) {
    FXL_LOG(ERROR) << "  Actual: " << event.phase;
    FXL_LOG(ERROR) << "Expected: " << phase;
    return false;
  } else if (event.x != x) {
    FXL_LOG(ERROR) << "  Actual: " << event.x;
    FXL_LOG(ERROR) << "Expected: " << x;
    return false;
  } else if (event.y != y) {
    FXL_LOG(ERROR) << "  Actual: " << event.y;
    FXL_LOG(ERROR) << "Expected: " << y;
    return false;
  }
  return true;
}

}  // namespace lib_ui_input_tests
