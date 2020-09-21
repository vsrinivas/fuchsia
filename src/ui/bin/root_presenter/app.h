// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_APP_H_
#define SRC_UI_BIN_ROOT_PRESENTER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/accessibility/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <limits>
#include <memory>
#include <vector>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/activity_notifier.h"
#include "src/ui/bin/root_presenter/color_transform_handler.h"
#include "src/ui/bin/root_presenter/factory_reset_manager.h"
#include "src/ui/bin/root_presenter/media_buttons_handler.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/safe_presenter.h"
#include "src/ui/lib/input_report_reader/input_reader.h"

namespace root_presenter {

// The presenter provides a |fuchsia::ui::policy::Presenter| service which
// displays UI by attaching the provided view to the root of a new view tree
// associated with a new renderer.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public fuchsia::ui::policy::Presenter,
            public fuchsia::ui::policy::DeviceListenerRegistry,
            public fuchsia::ui::input::InputDeviceRegistry,
            public fuchsia::ui::input::accessibility::PointerEventRegistry,
            public fuchsia::ui::views::accessibility::FocuserRegistry,
            public fuchsia::ui::views::Focuser,
            public ui_input::InputDeviceImpl::Listener {
 public:
  App(const fxl::CommandLine& command_line, async_dispatcher_t* dispatcher);
  ~App() = default;

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) override;
  void OnReport(ui_input::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

  // |fuchsia.ui.input.accessibility.PointerEventRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                    pointer_event_listener) override;

  // |fuchsia.ui.views.accessibility.FocuserRegistry|
  void RegisterFocuser(fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) override;

  // |Presenter|
  void PresentView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;

  // |Presenter|
  void PresentOrReplaceView(
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) override;

 private:
  // |DeviceListenerRegistry|
  void RegisterMediaButtonsListener(
      fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) override;

  // |InputDeviceRegistry|
  void RegisterDevice(
      fuchsia::ui::input::DeviceDescriptor descriptor,
      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) override;

  void InitializeServices();
  void Reset();

  void SetPresentation(std::unique_ptr<Presentation> presentation);
  void ShutdownPresentation();

  // |fuchsia.ui.views.Focuser|
  void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) override;

  std::unique_ptr<sys::ComponentContext> component_context_;
  fidl::BindingSet<fuchsia::ui::policy::Presenter> presenter_bindings_;
  fidl::BindingSet<fuchsia::ui::policy::DeviceListenerRegistry> device_listener_bindings_;
  fidl::BindingSet<fuchsia::ui::input::InputDeviceRegistry> input_receiver_bindings_;
  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventRegistry>
      a11y_pointer_event_bindings_;
  fidl::BindingSet<fuchsia::ui::views::accessibility::FocuserRegistry>
      a11y_focuser_registry_bindings_;
  ui_input::InputReader input_reader_;
  std::unique_ptr<FactoryResetManager> fdr_manager_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;

  ActivityNotifierImpl activity_notifier_;

  // This is a privileged interface between Root Presenter and Scenic. It forwards the requests
  // incoming from accessibility services to start listening for pointer events.
  fuchsia::ui::policy::accessibility::PointerEventRegistryPtr pointer_event_registry_;
  // This is a privileged interface between Root Presenter and Scenic. It forwards the Focuser
  // requests incoming from accessibility services to Scenic. This Focuser is implicitly associated
  // with the root view, which gives it access to change the Focus Chain to whatever view that is
  // part of the hierarchy.
  fuchsia::ui::views::FocuserPtr view_focuser_;
  // Binding holding the connection between a11y and Root Presenter. The incoming Focuser calls are
  // simply forwarded using |view_focuser_|.
  fidl::Binding<fuchsia::ui::views::Focuser> focuser_binding_;
  // If accessibility requests to register a Focuser and Scenic hasn't started yet, the binding of
  // |focuser_binding_| is deferred until |view_focuser_| is initialized.
  fit::function<void()> deferred_a11y_focuser_binding_;

  // If accessibility requests to register a PointerEventRegistry and Scenic hasn't started yet, the
  // registration is deferred until GetDisplayOwnershipEventCallback is called.
  fit::function<void()> deferred_a11y_pointer_event_registry_ = nullptr;

  // This is a privileged interface between Root Presenter and Accessibility. It allows Root
  // Presenter to register presentations with Accessibility for magnification.
  fuchsia::accessibility::MagnifierPtr magnifier_;

  // Today, we have a global, singleton compositor, and it is managed solely by
  // a root presenter. Hence, a single resource ID is sufficient to identify it.
  // Additionally, it is a system invariant that any compositor is created and
  // managed by a root presenter. We may relax these constraints in the
  // following order:
  // * Root presenter creates multiple compositors. Here, a resource ID for each
  //   compositor would still be sufficient to uniquely identify it.
  // * Root presenter delegates the creation of compositors. Here, we would
  //   need to generalize the identifier to include the delegate's session ID.
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  std::unique_ptr<scenic::LayerStack> layer_stack_;

  std::unique_ptr<Presentation> presentation_;

  std::unique_ptr<SafePresenter> safe_presenter_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<ui_input::InputDeviceImpl>> devices_by_id_;

  // The media button handler manages processing input from devices with media
  // buttons and propagating them to listeners.
  //
  // This processing is done at the global level through root presenter but
  // also supports registering listeners at the presentation level for legacy
  // support.
  MediaButtonsHandler media_buttons_handler_;

  // Tracks if scenic initialization is complete.
  bool is_scenic_initialized_ = false;
  std::unique_ptr<ColorTransformHandler> color_transform_handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_APP_H_
