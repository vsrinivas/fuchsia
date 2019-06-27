// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_CHILD_APP_H_
#define GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_CHILD_APP_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/eventpair.h>

#include <array>
#include <memory>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace direct_input_child {

// This is a child application that is started by direct_input.
//
// The README.md file describes its operation.
class App : public fuchsia::ui::app::ViewProvider {
 public:
  App(async::Loop* loop);
  ~App() override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

 private:
  const static std::size_t kMaxFingers = 10;

  // Not copyable or movable.
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  void UpdateScene(uint64_t next_presentation_time);
  void CreateScene(float display_width, float display_height);
  void ReleaseSessionResources();

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

  // Application fields
  std::unique_ptr<component::StartupContext> startup_context_;
  async::Loop* const message_loop_;

  // Scene fields
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::EntityNode> root_node_;
  std::unique_ptr<scenic::EntityNode> focus_frame_;
  std::array<std::unique_ptr<scenic::ShapeNode>, kMaxFingers> pointer_tracker_;
  std::array<uint32_t, kMaxFingers> pointer_id_;  // Pointer id for each shape.
  float width_in_px_;
  float height_in_px_;
  bool focused_;

  // View Provider fields
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  // View fields
  std::unique_ptr<scenic::View> view_;
};

}  // namespace direct_input_child

#endif  // GARNET_EXAMPLES_UI_LAB_DIRECT_INPUT_CHILD_APP_H_
