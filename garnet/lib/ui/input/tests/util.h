// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_TESTS_UTIL_H_
#define GARNET_LIB_UI_INPUT_TESTS_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/zx/eventpair.h>

#include <memory>
#include <string>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/input/input_system.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"
#include "lib/ui/gfx/tests/gfx_test.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace lib_ui_input_tests {

// Convenience function to reduce clutter.
void CreateTokenPair(zx::eventpair* t1, zx::eventpair* t2);

// Device-independent "display"; for testing only. Needed to ensure GfxSystem
// doesn't wait for a device-driven "display ready" signal.
class TestDisplay : public scenic_impl::gfx::Display {
 public:
  TestDisplay(uint64_t id, uint32_t width_px, uint32_t height_px)
      : Display(id, width_px, height_px) {}
  ~TestDisplay() = default;
  bool is_test_display() const override { return true; }
};

// Test fixture for exercising the input subsystem.
class InputSystemTest : public scenic_impl::test::ScenicTest {
 public:
  // For creation of a client-side session.
  scenic_impl::Scenic* scenic() { return scenic_.get(); }

  // Convenience function; triggers scene operations by scheduling the next
  // render task in the event loop.
  void RequestToPresent(scenic::Session* session);

  // Debugging function.
  std::string DumpScenes() { return gfx_->engine()->DumpScenes(); }

 protected:
  // Each test fixture defines its own test display parameters.  It's needed
  // both here (to define the display), and in the client (to define the size of
  // a layer (TODO(SCN-248)).
  virtual uint32_t test_display_width_px() const = 0;
  virtual uint32_t test_display_height_px() const = 0;

  // InputSystemTest needs its own teardown sequence, for session management.
  void TearDown() override;

  // Create a dummy GFX system, as well as a live input system to test.
  void InitializeScenic(scenic_impl::Scenic* scenic) override;

 private:
  std::unique_ptr<escher::impl::CommandBufferSequencer>
      command_buffer_sequencer_;
  scenic_impl::gfx::test::GfxSystemForTest* gfx_ = nullptr;
  scenic_impl::input::InputSystem* input_ = nullptr;
};

// Convenience wrapper to write Scenic clients with less boilerplate.
class SessionWrapper {
 public:
  SessionWrapper(scenic_impl::Scenic* scenic);
  ~SessionWrapper();

  // Allow caller to run some code in the context of this particular session.
  void RunNow(fit::function<void(scenic::Session* session,
                                 scenic::EntityNode* root_node)>
                  create_scene_callback);

  // Allow caller to examine the events received by this particular session.
  void ExamineEvents(
      fit::function<
          void(const std::vector<fuchsia::ui::input::InputEvent>& events)>
          examine_events_callback);

 protected:
  // Collects input events conveyed to this session.
  std::vector<fuchsia::ui::input::InputEvent> events_;

 private:
  // Client-side session object.
  std::unique_ptr<scenic::Session> session_;
  // Clients attach their nodes here to participate in the global scene graph.
  std::unique_ptr<scenic::EntityNode> root_node_;
};

// Creates pointer event commands for one finger, where the pointer "device" is
// tied to one compositor. Helps remove boilerplate clutter.
//
// NOTE: It's easy to create an event stream with inconsistent state, e.g.,
// sending ADD ADD.  Client is responsible for ensuring desired usage.
class PointerCommandGenerator {
 public:
  PointerCommandGenerator(scenic_impl::ResourceId compositor_id,
                          uint32_t device_id, uint32_t pointer_id,
                          fuchsia::ui::input::PointerEventType type);
  ~PointerCommandGenerator() = default;

  fuchsia::ui::input::Command Add(float x, float y);
  fuchsia::ui::input::Command Down(float x, float y);
  fuchsia::ui::input::Command Move(float x, float y);
  fuchsia::ui::input::Command Up(float x, float y);
  fuchsia::ui::input::Command Remove(float x, float y);

 private:
  fuchsia::ui::input::Command MakeInputCommand(
      fuchsia::ui::input::PointerEvent event);

  scenic_impl::ResourceId compositor_id_;
  fuchsia::ui::input::PointerEvent blank_;
};

// Creates keyboard event commands. Helps remove boilerplate clutter.
//
// NOTE: Just like PointerCommandGenerator, it's easy to create an event with
// inconsistent state. Client is responsible for ensuring desired usage.
class KeyboardCommandGenerator {
 public:
  KeyboardCommandGenerator(scenic_impl::ResourceId compositor_id,
                           uint32_t device_id);
  ~KeyboardCommandGenerator() = default;

  fuchsia::ui::input::Command Pressed(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Released(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Cancelled(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Repeat(uint32_t hid_usage, uint32_t modifiers);

 private:
  fuchsia::ui::input::Command MakeInputCommand(
      fuchsia::ui::input::KeyboardEvent event);

  scenic_impl::ResourceId compositor_id_;
  fuchsia::ui::input::KeyboardEvent blank_;
};

bool PointerMatches(const fuchsia::ui::input::PointerEvent& event,
                    uint32_t pointer_id,
                    fuchsia::ui::input::PointerEventPhase phase, float x,
                    float y);

}  // namespace lib_ui_input_tests

#endif  // GARNET_LIB_UI_INPUT_TESTS_UTIL_H_
