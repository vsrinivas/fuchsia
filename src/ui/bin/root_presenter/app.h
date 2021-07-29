// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_APP_H_
#define SRC_UI_BIN_ROOT_PRESENTER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/accessibility/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/constants.h"
#include "src/ui/bin/root_presenter/factory_reset_manager.h"
#include "src/ui/bin/root_presenter/focus_dispatcher.h"
#include "src/ui/bin/root_presenter/inspect.h"
#include "src/ui/bin/root_presenter/media_buttons_handler.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"
#include "src/ui/lib/input_report_reader/input_reader.h"

namespace root_presenter {

// Class for serving various input and graphics related APIs.
class App : public fuchsia::ui::policy::DeviceListenerRegistry,
            public fuchsia::ui::input::InputDeviceRegistry,
            public ui_input::InputDeviceImpl::Listener {
 public:
  App(sys::ComponentContext* component_context, fit::closure quit_callback);
  ~App() = default;

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) override;
  void OnReport(ui_input::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

  // For testing.
  Presentation* presentation() { return presentation_.get(); }

  // For testing.
  const inspect::Inspector* inspector() { return inspector_.inspector(); }

 private:
  // Exits the loop, terminating the RootPresenter process.
  void Exit() { quit_callback_(); }

  // |DeviceListenerRegistry|
  void RegisterMediaButtonsListener(
      fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) override;

  // |DeviceListenerRegistry|
  void RegisterListener(fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener,
                        RegisterListenerCallback callback) override;

  // |InputDeviceRegistry|
  void RegisterDevice(
      fuchsia::ui::input::DeviceDescriptor descriptor,
      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) override;

  const fit::closure quit_callback_;
  sys::ComponentInspector inspector_;
  InputReportInspector input_report_inspector_;
  fidl::BindingSet<fuchsia::ui::policy::DeviceListenerRegistry> device_listener_bindings_;
  fidl::BindingSet<fuchsia::ui::input::InputDeviceRegistry> input_receiver_bindings_;
  ui_input::InputReader input_reader_;
  FactoryResetManager fdr_manager_;

  fuchsia::ui::scenic::ScenicPtr scenic_;

  // Created at construction time.
  std::unique_ptr<Presentation> presentation_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<ui_input::InputDeviceImpl>> devices_by_id_;

  // The media button handler manages processing input from devices with media
  // buttons and propagating them to listeners.
  //
  // This processing is done at the global level through root presenter but
  // also supports registering listeners at the presentation level for legacy
  // support.
  MediaButtonsHandler media_buttons_handler_;

  // Used to dispatch the focus change messages to interested downstream clients.
  FocusDispatcher focus_dispatcher_;

  FidlBoundVirtualKeyboardCoordinator virtual_keyboard_coordinator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_APP_H_
