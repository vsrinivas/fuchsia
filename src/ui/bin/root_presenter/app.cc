// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/app.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/event.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "src/lib/files/file.h"
#include "src/ui/bin/root_presenter/safe_presenter.h"

namespace root_presenter {

App::App(const fxl::CommandLine& command_line, async_dispatcher_t* dispatcher)
    : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      input_reader_(this),
      fdr_manager_(std::make_unique<FactoryResetManager>(*component_context_.get(),
                                                         std::make_shared<MediaRetriever>())),
      activity_notifier_(dispatcher, ActivityNotifierImpl::kDefaultInterval,
                         *component_context_.get()),
      focuser_binding_(this),
      media_buttons_handler_(&activity_notifier_) {
  FX_DCHECK(component_context_);

  input_reader_.Start();

  component_context_->outgoing()->AddPublicService(presenter_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(device_listener_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(input_receiver_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(a11y_pointer_event_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(
      a11y_focuser_registry_bindings_.GetHandler(this));
}

void App::RegisterMediaButtonsListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) {
  media_buttons_handler_.RegisterListener(std::move(listener));
}

void App::PresentView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  if (presentation_) {
    FX_LOGS(ERROR) << "Support for multiple simultaneous presentations has been removed. To "
                      "replace a view, use PresentOrReplaceView";
    // Reject the request.
    presentation_request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  InitializeServices();

  int32_t display_startup_rotation_adjustment = 0;
  {
    std::string rotation_value;
    if (files::ReadFileToString("/config/data/display_rotation", &rotation_value)) {
      display_startup_rotation_adjustment = atoi(rotation_value.c_str());
      FX_LOGS(INFO) << "Display rotation adjustment applied: "
                    << display_startup_rotation_adjustment << " degrees.";
    }
  }

  auto presentation = std::make_unique<Presentation>(
      component_context_.get(), scenic_.get(), session_.get(), compositor_->id(),
      std::move(view_holder_token), std::move(presentation_request), safe_presenter_.get(),
      &activity_notifier_, display_startup_rotation_adjustment,
      /*on_client_death*/ [this] { ShutdownPresentation(); });

  SetPresentation(std::move(presentation));
}

void App::PresentOrReplaceView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  if (presentation_) {
    ShutdownPresentation();
  }
  PresentView(std::move(view_holder_token), std::move(presentation_request));
}

void App::SetPresentation(std::unique_ptr<Presentation> presentation) {
  FX_DCHECK(presentation);
  FX_DCHECK(!presentation_);
  presentation_ = std::move(presentation);

  for (auto& it : devices_by_id_) {
    presentation_->OnDeviceAdded(it.second.get());
  }

  layer_stack_->AddLayer(presentation_->layer());
  if (magnifier_) {
    presentation_->RegisterWithMagnifier(magnifier_.get());
  }

  safe_presenter_->QueuePresent([] {});
}

void App::ShutdownPresentation() {
  presentation_.reset();
  layer_stack_->RemoveAllLayers();
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
    if (presentation_)
      presentation_->OnDeviceAdded(input_device.get());
  }

  devices_by_id_.emplace(device_id, std::move(input_device));
}

void App::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FX_VLOGS(1) << "UnregisterDevice " << input_device->id();

  if (!media_buttons_handler_.OnDeviceRemoved(input_device->id())) {
    if (presentation_)
      presentation_->OnDeviceRemoved(input_device->id());
  }

  devices_by_id_.erase(input_device->id());
}

void App::OnReport(ui_input::InputDeviceImpl* input_device,
                   fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "root_presenter_on_report", "id", report.trace_id);
  TRACE_FLOW_END("input", "report_to_presenter", report.trace_id);

  FX_VLOGS(3) << "OnReport from " << input_device->id() << " " << report;

  if (devices_by_id_.count(input_device->id()) == 0) {
    return;
  }

  // TODO(fxbug.dev/36217): Do not clone once presentation stops needing input.
  fuchsia::ui::input::InputReport cloned_report;
  report.Clone(&cloned_report);

  if (report.media_buttons) {
    if (fdr_manager_->OnMediaButtonReport(*(report.media_buttons.get()))) {
      return;
    }

    media_buttons_handler_.OnReport(input_device->id(), std::move(cloned_report));
    return;
  }

  if (!presentation_) {
    return;
  }

  // Input events are only reported to the active presentation.
  TRACE_FLOW_BEGIN("input", "report_to_presentation", report.trace_id);
  presentation_->OnReport(input_device->id(), std::move(report));
}

void App::InitializeServices() {
  if (!scenic_) {
    component_context_->svc()->Connect(scenic_.NewRequest());
    scenic_.set_error_handler([this](zx_status_t error) {
      FX_LOGS(ERROR) << "Scenic died, destroying presentation.";
      Reset();
    });

    session_ = std::make_unique<scenic::Session>(scenic_.get(), view_focuser_.NewRequest());
    session_->set_error_handler([this](zx_status_t error) {
      FX_LOGS(ERROR) << "Session died, destroying presentation.";
      Reset();
    });

    safe_presenter_ = std::make_unique<SafePresenter>(session_.get());

    // Globally disable parallel dispatch of input events.
    // TODO(fxbug.dev/24258): Enable parallel dispatch.
    {
      fuchsia::ui::input::SetParallelDispatchCmd cmd;
      cmd.parallel_dispatch = false;
      fuchsia::ui::input::Command input_cmd;
      input_cmd.set_set_parallel_dispatch(std::move(cmd));
      session_->Enqueue(std::move(input_cmd));
    }

    compositor_ = std::make_unique<scenic::DisplayCompositor>(session_.get());
    layer_stack_ = std::make_unique<scenic::LayerStack>(session_.get());
    compositor_->SetLayerStack(*layer_stack_.get());
    safe_presenter_->QueuePresent([] {});

    scenic_->GetDisplayOwnershipEvent([this](zx::event event) {
      input_reader_.SetOwnershipEvent(std::move(event));
      is_scenic_initialized_ = true;
      if (deferred_a11y_pointer_event_registry_) {
        // Process pending pointer event register requests.
        deferred_a11y_pointer_event_registry_();
        deferred_a11y_pointer_event_registry_ = nullptr;
      }
    });

    component_context_->svc()->Connect(magnifier_.NewRequest());
    // No need to set an error handler here unless we want to attempt a reconnect or something;
    // instead, we add error handlers for cleanup on the a11y presentations when we register them.

    // Add Color Transform Handler.
    color_transform_handler_ = std::make_unique<ColorTransformHandler>(
        component_context_.get(), compositor_->id(), session_.get(), safe_presenter_.get());

    // If a11y tried to register a Focuser while Scenic wasn't ready yet, bind the request now.
    if (deferred_a11y_focuser_binding_) {
      deferred_a11y_focuser_binding_();
      deferred_a11y_focuser_binding_ = nullptr;
    }
  }
}

void App::Reset() {
  presentation_.reset();             // must be first, holds pointers to services
  color_transform_handler_.reset();  // session_ ptr may not be valid
  layer_stack_ = nullptr;
  compositor_ = nullptr;
  session_ = nullptr;
  scenic_.Unbind();
}

void App::Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                       pointer_event_listener) {
  if (!is_scenic_initialized_) {
    if (deferred_a11y_pointer_event_registry_) {
      FX_LOGS(ERROR)
          << "Unable to defer a new request. A11y pointer event registration is already deferred.";
      return;
    }
    deferred_a11y_pointer_event_registry_ =
        [this, pointer_event_listener = std::move(pointer_event_listener)]() mutable {
          Register(std::move(pointer_event_listener));
        };

    return;
  }

  if (!pointer_event_registry_) {
    component_context_->svc()->Connect(pointer_event_registry_.NewRequest());
    pointer_event_registry_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Pointer event registry died with error:" << zx_status_get_string(status);
    });
  }
  FX_LOGS(INFO) << "Connecting to pointer event registry.";
  // Forward the listener to the registry we're connected to.
  auto callback = [](bool status) {
    FX_LOGS(INFO) << "Registration completed for pointer event registry with status: " << status;
  };

  pointer_event_registry_->Register(std::move(pointer_event_listener), std::move(callback));
}

void App::RegisterFocuser(fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  if (!view_focuser_) {
    // Scenic hasn't started yet, so defer the binding of the incoming focuser request.
    // Similar to the case below, drop any old focuser binding request and defer the new one.
    deferred_a11y_focuser_binding_ = [this, view_focuser = std::move(view_focuser)]() mutable {
      RegisterFocuser(std::move(view_focuser));
    };
    return;
  }
  if (focuser_binding_.is_bound()) {
    FX_LOGS(INFO) << "Registering a new Focuser for a11y. Dropping the old one.";
  }
  focuser_binding_.Bind(std::move(view_focuser));
}

void App::RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) {
  if (!view_focuser_) {
    // Scenic disconnected, close the connection.
    callback(fit::error(fuchsia::ui::views::Error::DENIED));
    focuser_binding_.Close(ZX_ERR_BAD_STATE);
  } else {
    view_focuser_->RequestFocus(std::move(view_ref), std::move(callback));
  }
}

}  // namespace root_presenter
