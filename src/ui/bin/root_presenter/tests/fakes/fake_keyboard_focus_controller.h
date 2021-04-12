// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_KEYBOARD_FOCUS_CONTROLLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_KEYBOARD_FOCUS_CONTROLLER_H_

#include <fuchsia/ui/keyboard/focus/cpp/fidl.h>
#include <fuchsia/ui/keyboard/focus/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

namespace root_presenter::testing {

// A fake server for "fuchsia.ui.keyboard.focus.Controller".
//
// It does very little: it can be bound as a server for this protocol, responds with a success on
// each call to Notify (the only method), and keeps a count on how many times Notify has been
// called.
class FakeKeyboardFocusController
    : public fuchsia::ui::keyboard::focus::testing::Controller_TestBase {
 public:
  FakeKeyboardFocusController();

  // Creates a new fake keyboard focus controller.  The `context_provider` is used to
  // connect to the FIDL endpoints needed.
  explicit FakeKeyboardFocusController(sys::testing::ComponentContextProvider& context_provider);

  ~FakeKeyboardFocusController() override = default;

  // Call to get a working handler for this protocol.  It still needs to be exposed as a service.
  fidl::InterfaceRequestHandler<fuchsia::ui::keyboard::focus::Controller> GetHandler(
      async_dispatcher_t* dispatcher = nullptr);

  // Sets a callback to be invoked when a `Notify` call is received.  The callback will be passed
  // the value of the ViewRef that was forwarded.
  void SetOnNotify(std::function<void(const fuchsia::ui::views::ViewRef&)> on_notify_callback);

  void NotImplemented_(const std::string& name) final {}

  // Implements `fuchsia.ui.keyboard.focus.Controller.Notify`.
  void Notify(fuchsia::ui::views::ViewRef view_ref, NotifyCallback callback) override;

  // Returns the number of calls issued to this fake.
  int num_calls() const { return num_calls_; }

  // Returns the kernel object ID (koid) of the last view ref that was received.
  zx_koid_t get_last_view_ref_koid();

 private:
  fidl::BindingSet<fuchsia::ui::keyboard::focus::Controller> bindings_;
  int num_calls_{};
  std::function<void(const fuchsia::ui::views::ViewRef& received_view_ref)> on_notify_callback_;
};

}  // namespace root_presenter::testing

#endif  // SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_KEYBOARD_FOCUS_CONTROLLER_H_
