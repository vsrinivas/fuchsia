// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/headless_root_presenter/app.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <zircon/status.h>

#include <trace/event.h>

#include "src/lib/fxl/logging.h"

namespace headless_root_presenter {

App::App(const fxl::CommandLine& command_line, async::Loop* loop,
         std::unique_ptr<sys::ComponentContext> component_context)
    : component_context_(std::move(component_context)),
      input_reader_(this),
      fdr_manager_(
          std::make_unique<root_presenter::FactoryResetManager>(*component_context_.get())),
      activity_notifier_(loop->dispatcher(), root_presenter::ActivityNotifierImpl::kDefaultInterval,
                         *component_context_.get()),
      media_buttons_handler_(&activity_notifier_) {
  FXL_DCHECK(component_context_);

  input_reader_.Start();

  component_context_->outgoing()->AddPublicService(device_listener_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(input_receiver_bindings_.GetHandler(this));
}

void App::RegisterMediaButtonsListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) {
  media_buttons_handler_.RegisterListener(std::move(listener));
}

void App::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FXL_VLOG(1) << "RegisterDevice " << device_id << " " << descriptor;
  std::unique_ptr<ui_input::InputDeviceImpl> input_device =
      std::make_unique<ui_input::InputDeviceImpl>(device_id, std::move(descriptor),
                                                  std::move(input_device_request), this);

  media_buttons_handler_.OnDeviceAdded(input_device.get());

  devices_by_id_.emplace(device_id, std::move(input_device));
}

void App::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FXL_VLOG(1) << "UnregisterDevice " << input_device->id();

  media_buttons_handler_.OnDeviceRemoved(input_device->id());

  devices_by_id_.erase(input_device->id());
}

void App::OnReport(ui_input::InputDeviceImpl* input_device,
                   fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "headless_root_presenter_on_report", "id", report.trace_id);
  TRACE_FLOW_END("input", "report_to_presenter", report.trace_id);

  FXL_VLOG(1) << "OnReport from " << input_device->id() << " " << report;

  if (devices_by_id_.count(input_device->id()) == 0) {
    return;
  }

  if (report.media_buttons) {
    fuchsia::ui::input::InputReport cloned_report;
    report.Clone(&cloned_report);

    media_buttons_handler_.OnReport(input_device->id(), std::move(cloned_report));
    fdr_manager_->OnMediaButtonReport(*(report.media_buttons.get()));
    return;
  }
}

}  // namespace headless_root_presenter
