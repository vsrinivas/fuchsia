// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/app.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/keyboard/focus/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/event.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "src/lib/files/file.h"

namespace root_presenter {

namespace {
void ChattyLog(const fuchsia::ui::input::InputReport& report) {
  static uint32_t chatty = 0;
  if (chatty++ < ChattyMax()) {
    FX_LOGS(INFO) << "Rp-App[" << chatty << "/" << ChattyMax() << "]: " << report;
  }
}
}  // namespace

App::App(sys::ComponentContext* component_context, fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)),
      inspector_(component_context),
      input_report_inspector_(inspector_.root().CreateChild("input_reports")),
      input_reader_(this),
      fdr_manager_(*component_context, std::make_shared<MediaRetriever>()),
      media_buttons_handler_(),
      virtual_keyboard_coordinator_(component_context),
      focus_dispatcher_(component_context->svc(), virtual_keyboard_coordinator_.GetWeakPtr()) {
  FX_DCHECK(component_context);

  input_reader_.Start();

  component_context->outgoing()->AddPublicService(device_listener_bindings_.GetHandler(this));
  component_context->outgoing()->AddPublicService(input_receiver_bindings_.GetHandler(this));

  component_context->svc()->Connect(scenic_.NewRequest());
  scenic_.set_error_handler([this](zx_status_t error) {
    FX_LOGS(WARNING) << "Scenic died with error " << zx_status_get_string(error)
                     << ". Killing RootPresenter.";
    Exit();
  });

  scenic_->GetDisplayOwnershipEvent(
      [this](zx::event event) { input_reader_.SetOwnershipEvent(std::move(event)); });

  int32_t display_startup_rotation_adjustment = 0;
  {
    std::string rotation_value;
    if (files::ReadFileToString("/config/data/display_rotation", &rotation_value)) {
      display_startup_rotation_adjustment = atoi(rotation_value.c_str());
      FX_LOGS(INFO) << "Display rotation adjustment applied: "
                    << display_startup_rotation_adjustment << " degrees.";
    }
  }

  // Set up root session.
  // Create session and session listener.
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::views::FocuserPtr focuser_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener;
  auto session_request = session_ptr.NewRequest();

  // Wrap root session for convenience and create valid session listener.
  auto session =
      std::make_unique<scenic::Session>(std::move(session_ptr), session_listener.NewRequest());

  // Create scenic session.
  scenic_->CreateSession2(std::move(session_request), session_listener.Bind(),
                          focuser_ptr.NewRequest());

  presentation_ = std::make_unique<Presentation>(
      inspector_.root().CreateChild(inspector_.root().UniqueName("presentation-")),
      component_context, scenic_.get(), std::move(session), std::move(focuser_ptr),
      display_startup_rotation_adjustment);

  for (auto& it : devices_by_id_) {
    presentation_->OnDeviceAdded(it.second.get());
  }

  FX_DCHECK(scenic_);
  FX_DCHECK(presentation_)
      << "All service handlers must be set up and published prior to loop.Run() in main.cc";
}

void App::RegisterMediaButtonsListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) {
  media_buttons_handler_.RegisterListener(std::move(listener));
}

void App::RegisterListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener,
    RegisterListenerCallback callback) {
  media_buttons_handler_.RegisterListener2(std::move(listener));
  callback();
}

void App::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FX_VLOGS(1) << "RegisterDevice " << device_id << " " << descriptor;
  std::unique_ptr<ui_input::InputDeviceImpl> input_device =
      std::make_unique<ui_input::InputDeviceImpl>(device_id, std::move(descriptor),
                                                  std::move(input_device_request), this);

  // Media button processing is done exclusively in root_presenter::App.
  // Dependent components inside presentations register with the handler (passed
  // at construction) to receive such events.
  if (!media_buttons_handler_.OnDeviceAdded(input_device.get())) {
    presentation_->OnDeviceAdded(input_device.get());
  }

  devices_by_id_.emplace(device_id, std::move(input_device));
}

void App::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FX_VLOGS(1) << "UnregisterDevice " << input_device->id();

  if (!media_buttons_handler_.OnDeviceRemoved(input_device->id())) {
    presentation_->OnDeviceRemoved(input_device->id());
  }

  devices_by_id_.erase(input_device->id());
}

void App::OnReport(ui_input::InputDeviceImpl* input_device,
                   fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "root_presenter_on_report", "id", report.trace_id);
  TRACE_FLOW_END("input", "report_to_presenter", report.trace_id);

  FX_VLOGS(3) << "OnReport from " << input_device->id() << " " << report;
  ChattyLog(report);
  input_report_inspector_.OnInputReport(report);

  if (devices_by_id_.count(input_device->id()) == 0) {
    return;
  }

  // TODO(fxbug.dev/36217): Do not clone once presentation stops needing input.
  fuchsia::ui::input::InputReport cloned_report;
  report.Clone(&cloned_report);

  if (cloned_report.media_buttons) {
    fdr_manager_.OnMediaButtonReport(*(cloned_report.media_buttons.get()));
    media_buttons_handler_.OnReport(input_device->id(), std::move(cloned_report));
    return;
  }

  // Input events are only reported to the active presentation.
  TRACE_FLOW_BEGIN("input", "report_to_presentation", report.trace_id);
  presentation_->OnReport(input_device->id(), std::move(report));
}

}  // namespace root_presenter
