// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TESTS_UTIL_H_
#define SRC_UI_SCENIC_LIB_INPUT_TESTS_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>

#include <memory>
#include <string>

#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace lib_ui_input_tests {

// Convenience wrapper to write Scenic clients with less boilerplate.
class SessionWrapper {
 public:
  SessionWrapper(scenic_impl::Scenic* scenic);
  SessionWrapper(SessionWrapper&&);
  virtual ~SessionWrapper();

  scenic::Session* session() { return session_.get(); }
  std::vector<fuchsia::ui::input::InputEvent>& events() { return events_; }
  const std::vector<fuchsia::ui::input::InputEvent>& events() const { return events_; }

  void SetView(std::unique_ptr<scenic::View> view) { view_ = std::move(view); }
  void SetViewRef(fuchsia::ui::views::ViewRef view_ref) {
    view_ref_ = std::move(view_ref);
    SetViewKoid(utils::ExtractKoid(view_ref_.value()));
  }
  void SetViewKoid(zx_koid_t koid) { view_koid_ = koid; }
  zx_koid_t ViewKoid() const {
    FX_CHECK(view_koid_) << "No view koid set.";
    return view_koid_;
  }

  fuchsia::ui::views::ViewRef view_ref() {
    FX_CHECK(view_ref_) << "No ViewRef set.";
    fuchsia::ui::views::ViewRef clone;
    view_ref_->Clone(&clone);
    return clone;
  }

  scenic::View* view() { return view_.get(); }

 private:
  // Callback to capture returned events.
  void OnEvent(std::vector<fuchsia::ui::scenic::Event> events);

  // Client-side session object.
  std::unique_ptr<scenic::Session> session_;
  // View koid, if any.
  zx_koid_t view_koid_ = ZX_KOID_INVALID;
  // View, if any.
  std::unique_ptr<scenic::View> view_;
  // ViewRef, if any.
  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
  // Collects input events conveyed to this session.
  std::vector<fuchsia::ui::input::InputEvent> events_;
};

// https://fuchsia.dev/fuchsia-src/concepts/graphics/ui/scenic#scenic_resource_graph
struct ResourceGraph {
  ResourceGraph(scenic::Session* session);

  scenic::Scene scene;
  scenic::Camera camera;
  scenic::Renderer renderer;
  scenic::Layer layer;
  scenic::LayerStack layer_stack;
  scenic::Compositor compositor;
};

// Test fixture for exercising the input subsystem.
class InputSystemTest : public scenic_impl::test::ScenicTest {
 public:
  // Sensible 5x5x1 view bounds for a |scenic::ViewHolder| for a test view configured using
  // |SetUpTestView|.
  static constexpr fuchsia::ui::gfx::ViewProperties k5x5x1 = {.bounding_box = {.max = {5, 5, 1}}};
  // clang-format off
  static constexpr std::array<float, 9> kIdentityMatrix = {
    1.f, 0.f, 0.f, // first column
    0.f, 1.f, 0.f, // second column
    0.f, 0.f, 1.f, // third column
  };
  // clang-format on

  // Convenience function; triggers scene operations by scheduling the next
  // render task in the event loop.
  void RequestToPresent(scenic::Session* session);

  scenic_impl::input::InputSystem* input_system() { return input_system_; }

  scenic_impl::gfx::Engine* engine() { return engine_.get(); }

  // Each test fixture defines its own test display parameters.  It's needed
  // both here (to define the display), and in the client (to define the size of
  // a layer (TODO(fxbug.dev/23494)).
  virtual uint32_t test_display_width_px() const = 0;
  virtual uint32_t test_display_height_px() const = 0;

  // Creates a root session and empty scene, sizing the layer to display dimensions.
  std::pair<SessionWrapper, ResourceGraph> CreateScene();

  // Sets up a view containing a 5x5 rectangle centered at (2, 2).
  void SetUpTestView(scenic::View* view);

  // Creates a test session with a view containing a 5x5 rectangle centered at (2, 2).
  SessionWrapper CreateClient(const std::string& name, fuchsia::ui::views::ViewToken view_token);

  // Inject an event. Must have first called RegisterInjector.
  void Inject(float x, float y, fuchsia::ui::pointerinjector::EventPhase phase);

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref,
                        fuchsia::ui::pointerinjector::DispatchPolicy dispatch_policy,
                        fuchsia::ui::pointerinjector::DeviceType type,
                        std::array<std::array<float, 2>, 2> extents,
                        std::array<float, 9> viewport_matrix = kIdentityMatrix);

  // |testing::Test|
  // InputSystemTest needs its own teardown sequence, for session management.
  void TearDown() override;

 private:
  // |scenic_impl::test::ScenicTest|
  // Create a dummy GFX system, as well as a live input system to test.
  void InitializeScenic(std::shared_ptr<scenic_impl::Scenic> scenic) override;

  sys::testing::ComponentContextProvider context_provider_;
  std::shared_ptr<scenic_impl::gfx::Engine> engine_;
  std::shared_ptr<scenic_impl::display::Display> display_;

  scenic_impl::input::InputSystem* input_system_ = nullptr;
  fuchsia::ui::pointerinjector::DevicePtr injector_;
};

// Creates pointer event commands for one finger, where the pointer "device" is
// tied to one compositor. Helps remove boilerplate clutter.
//
// NOTE: It's easy to create an event stream with inconsistent state, e.g.,
// sending ADD ADD.  Client is responsible for ensuring desired usage.
class PointerCommandGenerator {
 public:
  PointerCommandGenerator(scenic_impl::ResourceId compositor_id, uint32_t device_id,
                          uint32_t pointer_id, fuchsia::ui::input::PointerEventType type,
                          uint32_t buttons = 0);
  ~PointerCommandGenerator() = default;

  fuchsia::ui::input::Command Add(float x, float y);
  fuchsia::ui::input::Command Down(float x, float y);
  fuchsia::ui::input::Command Move(float x, float y);
  fuchsia::ui::input::Command Up(float x, float y);
  fuchsia::ui::input::Command Remove(float x, float y);

 private:
  fuchsia::ui::input::Command MakeInputCommand(fuchsia::ui::input::PointerEvent event);

  scenic_impl::ResourceId compositor_id_;
  fuchsia::ui::input::PointerEvent blank_;
};

// Creates keyboard event commands. Helps remove boilerplate clutter.
//
// NOTE: Just like PointerCommandGenerator, it's easy to create an event with
// inconsistent state. Client is responsible for ensuring desired usage.
class KeyboardCommandGenerator {
 public:
  KeyboardCommandGenerator(scenic_impl::ResourceId compositor_id, uint32_t device_id);
  ~KeyboardCommandGenerator() = default;

  fuchsia::ui::input::Command Pressed(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Released(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Cancelled(uint32_t hid_usage, uint32_t modifiers);
  fuchsia::ui::input::Command Repeat(uint32_t hid_usage, uint32_t modifiers);

 private:
  fuchsia::ui::input::Command MakeInputCommand(fuchsia::ui::input::KeyboardEvent event);

  scenic_impl::ResourceId compositor_id_;
  fuchsia::ui::input::KeyboardEvent blank_;
};

bool PointerMatches(
    const fuchsia::ui::input::PointerEvent& event, uint32_t pointer_id,
    fuchsia::ui::input::PointerEventPhase phase, float x, float y,
    fuchsia::ui::input::PointerEventType type = fuchsia::ui::input::PointerEventType::TOUCH,
    uint32_t buttons = 0);

}  // namespace lib_ui_input_tests

#endif  // SRC_UI_SCENIC_LIB_INPUT_TESTS_UTIL_H_
