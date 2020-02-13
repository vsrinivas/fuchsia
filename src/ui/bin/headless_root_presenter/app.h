// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_APP_H_
#define SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_APP_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/bin/root_presenter/activity_notifier.h"
#include "src/ui/bin/root_presenter/media_buttons_handler.h"
#include "src/ui/bin/root_presenter/factory_reset_manager.h"
#include "src/ui/lib/input_reader/input_reader.h"

namespace headless_root_presenter {

// The headless root presenter connects and tracks input devices, receives
// input reports, displatch them to media buttons handlers, factory reset manager,
// activity manager.
class App : public fuchsia::ui::policy::DeviceListenerRegistry,
            public fuchsia::ui::input::InputDeviceRegistry,
            public ui_input::InputDeviceImpl::Listener {
 public:
  App(const fxl::CommandLine& command_line, async::Loop* loop,
      std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create());
  ~App() = default;

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) override;
  void OnReport(ui_input::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

 private:
  // |DeviceListenerRegistry|
  void RegisterMediaButtonsListener(
      fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) override;

  // |InputDeviceRegistry|
  void RegisterDevice(
      fuchsia::ui::input::DeviceDescriptor descriptor,
      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) override;

  std::unique_ptr<sys::ComponentContext> component_context_;
  fidl::BindingSet<fuchsia::ui::policy::DeviceListenerRegistry> device_listener_bindings_;
  fidl::BindingSet<fuchsia::ui::input::InputDeviceRegistry> input_receiver_bindings_;
  ui_input::InputReader input_reader_;

  std::unique_ptr<root_presenter::FactoryResetManager> fdr_manager_;
  root_presenter::ActivityNotifierImpl activity_notifier_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<ui_input::InputDeviceImpl>> devices_by_id_;

  // The media button handler processes input from devices with media
  // buttons and propagates them to listeners.
  root_presenter::MediaButtonsHandler media_buttons_handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace headless_root_presenter

#endif  // SRC_UI_BIN_HEADLESS_ROOT_PRESENTER_APP_H_
