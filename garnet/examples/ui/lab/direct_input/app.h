// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_APP_H_
#define GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_APP_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/eventpair.h>

#include <array>
#include <memory>

#include "garnet/bin/ui/input_reader/input_reader.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/ui/input/device_state.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace direct_input {

// The direct_input application is a standalone application that exercises
// Scenic's input subsystem. To run it:
// $ run direct_input [--verbose=1]
//
// The README.md file describes its operation.
class App : public fuchsia::ui::input::InputDeviceRegistry,
            public ui_input::InputDeviceImpl::Listener {
 public:
  App(async::Loop* loop);
  ~App();

  // |fuchsia::ui::input::InputDeviceRegistry|
  void RegisterDevice(fuchsia::ui::input::DeviceDescriptor descriptor,
                      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                          input_device) override;

  // |ui_input::InputDeviceImpl::Listener|
  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) override;

  // |ui_input::InputDeviceImpl::Listener|
  void OnReport(ui_input::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

 private:
  const static std::size_t kMaxFingers = 10;

  // Not copyable or movable.
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  void UpdateScene(uint64_t next_presentation_time);
  void CreateScene(float display_width, float display_height);
  void ReleaseSessionResources();

  // Press ESC to quit.
  void CheckQuit(const fuchsia::ui::input::InputEvent& event);

  // Callbacks
  void OnScenicError();
  void OnSessionError();
  void OnSessionClose();
  // Deal with Events from Scenic.
  void OnSessionEvents(std::vector<fuchsia::ui::scenic::Event> events);
  // Display a focus frame around the View.
  void OnFocusEvent(const fuchsia::ui::input::FocusEvent& event);
  // Blink the focus frame.
  void OnKeyboardEvent(const fuchsia::ui::input::KeyboardEvent& event);
  // Display a finger tracker for each finger captured by this View.
  void OnPointerEvent(const fuchsia::ui::input::PointerEvent& event);
  // Deal with sensor reports received from Zircon.
  void OnDeviceSensorEvent(uint32_t device_id,
                           fuchsia::ui::input::InputReport event);
  // Route input events from Zircon to Scenic.
  void OnDeviceInputEvent(uint32_t compositor_id,
                          fuchsia::ui::input::InputEvent event);

  // Application fields
  std::unique_ptr<component::StartupContext> startup_context_;
  async::Loop* const message_loop_;

  // Input fields
  ui_input::InputReader input_reader_;
  fidl::BindingSet<fuchsia::ui::input::InputDeviceRegistry>
      input_device_registry_bindings_;
  uint32_t next_device_token_;
  std::unordered_map<uint32_t, std::unique_ptr<ui_input::InputDeviceImpl>>
      device_by_id_;
  std::unordered_map<uint32_t, std::unique_ptr<ui_input::DeviceState>>
      device_state_by_id_;

  // DirectInput's Scene
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  std::unique_ptr<scenic::Camera> camera_;
  std::unique_ptr<scenic::EntityNode> focus_frame_;
  std::array<std::unique_ptr<scenic::ShapeNode>, kMaxFingers> pointer_tracker_;
  std::array<uint32_t, kMaxFingers> pointer_id_;  // Pointer id for each shape.
  float width_in_px_;
  float height_in_px_;
  bool focused_;

  // DirectInput's View
  std::unique_ptr<scenic::ViewHolder> view_holder_;
  std::unique_ptr<scenic::View> view_;

  // Child component fields
  fuchsia::sys::ComponentControllerPtr child_controller_;
  fuchsia::ui::app::ViewProviderPtr child_view_provider_;
  std::unique_ptr<scenic::ViewHolder> child_view_holder_;
};

}  // namespace direct_input

#endif  // GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_APP_H_
