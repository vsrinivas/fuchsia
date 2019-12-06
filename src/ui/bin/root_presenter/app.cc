// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/app.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/event.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include <trace/event.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace root_presenter {

App::App(const fxl::CommandLine& command_line, async::Loop* loop)
    : component_context_(sys::ComponentContext::Create()),
      input_reader_(this),
      fdr_manager_(std::make_unique<FactoryResetManager>(*component_context_.get())),
      activity_notifier_(loop->dispatcher(), ActivityNotifierImpl::kDefaultInterval,
                         *component_context_.get()),
      media_buttons_handler_(&activity_notifier_) {
  FXL_DCHECK(component_context_);

  input_reader_.Start();

  component_context_->outgoing()->AddPublicService(presenter_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(device_listener_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(input_receiver_bindings_.GetHandler(this));
  component_context_->outgoing()->AddPublicService(a11y_pointer_event_bindings_.GetHandler(this));
}

void App::RegisterMediaButtonsListener(
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener) {
  media_buttons_handler_.RegisterListener(std::move(listener));
}

void App::PresentView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  PresentView(std::move(view_holder_token), std::move(presentation_request),
              /*clobber_previous_presentation=*/false);
}

void App::PresentOrReplaceView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  PresentView(std::move(view_holder_token), std::move(presentation_request),
              /*clobber_previous_presentation=*/true);
}

void App::PresentView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request,
    bool clobber_previous_presentation) {
  InitializeServices();

  int32_t display_startup_rotation_adjustment = 0;
  {
    std::string rotation_value;
    if (files::ReadFileToString("/config/data/display_rotation", &rotation_value)) {
      display_startup_rotation_adjustment = atoi(rotation_value.c_str());
      FXL_LOG(INFO) << "Display rotation adjustment applied: "
                    << display_startup_rotation_adjustment << " degrees.";
    }
  }

  fuchsia::ui::shortcut::Manager* shortcut_manager =
      shortcut_manager_ ? shortcut_manager_.get() : nullptr;

  auto presentation = std::make_unique<Presentation>(
      scenic_.get(), session_.get(), compositor_->id(), std::move(view_holder_token),
      std::move(presentation_request), shortcut_manager, ime_service_.get(), &activity_notifier_,
      renderer_params_, display_startup_rotation_adjustment,
      [this](bool yield_to_next) {
        if (yield_to_next) {
          SwitchToNextPresentation();
        } else {
          SwitchToPreviousPresentation();
        }
      },
      &media_buttons_handler_);

  SetPresentation(std::move(presentation), clobber_previous_presentation);
}

void App::SetPresentation(std::unique_ptr<Presentation> presentation, bool clobber_presentation) {
  if (clobber_presentation && !presentations_.empty()) {
    ReplacePresentationWith(std::move(presentation));
  } else {
    AddPresentation(std::move(presentation));
  }
}

void App::AddPresentation(std::unique_ptr<Presentation> presentation) {
  // TODO(41929): Once we're confident no one is using multiple presentations, assert this never
  // happens.
  if (!presentations_.empty()) {
    FXL_LOG(WARNING)
        << "Using multiple presentations is deprecated. Call PresentOrReplaceView() to "
           "force replacement of current presentation.";
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
  }

  for (auto& it : devices_by_id_) {
    presentation->OnDeviceAdded(it.second.get());
  }

  presentations_.push_back(std::move(presentation));
  SwitchToPresentation(presentations_.size() - 1);
}

void App::ReplacePresentationWith(std::unique_ptr<Presentation> presentation) {
  FXL_DCHECK(presentations_.size() == 1)
      << "Can only replace presentation when there is a single instance.";
  ShutdownPresentation(/*presentation_idx=*/0);
  AddPresentation(std::move(presentation));
}

void App::HACK_SetRendererParams(bool enable_clipping,
                                 std::vector<fuchsia::ui::gfx::RendererParam> params) {
  renderer_params_.clipping_enabled = enable_clipping;
  FXL_LOG(INFO) << "Presenter::HACK_SetRendererParams: Setting clipping enabled to "
                << (enable_clipping ? "true" : "false");
  for (auto& param : params) {
    switch (param.Which()) {
      case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
        renderer_params_.shadow_technique = param.shadow_technique();
        FXL_LOG(INFO) << "Presenter::HACK_SetRendererParams: Setting shadow technique to "
                      << fidl::ToUnderlying(param.shadow_technique());
        continue;
      case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
        renderer_params_.render_frequency = param.render_frequency();
        FXL_LOG(INFO) << "Presenter::HACK_SetRendererParams: Setting render frequency to "
                      << fidl::ToUnderlying(param.render_frequency());
        continue;
      case fuchsia::ui::gfx::RendererParam::Tag::kEnableDebugging:
        renderer_params_.debug_enabled = param.enable_debugging();
        FXL_LOG(INFO) << "Presenter::HACK_SetRendererParams: Setting debug enabled to "
                      << param.enable_debugging();
        continue;
      case fuchsia::ui::gfx::RendererParam::Tag::Invalid:
        continue;
    }
  }
  for (const auto& presentation : presentations_) {
    presentation->OverrideRendererParams(renderer_params_);
  }
}

void App::ShutdownPresentation(size_t presentation_idx) {
  if (presentation_idx == active_presentation_idx_) {
    // This works fine when idx == 0, because the previous idx
    // chosen will also be 0, and it will be an no-op within
    // SwitchToPreviousPresentation. Finally, at the end of the
    // callback, everything will be cleaned up.
    SwitchToPreviousPresentation();
  }

  presentations_.erase(presentations_.begin() + presentation_idx);
  if (presentation_idx < active_presentation_idx_) {
    // Adjust index into presentations_.
    active_presentation_idx_--;
  }

  if (presentations_.empty()) {
    layer_stack_->RemoveAllLayers();
    active_presentation_idx_ = std::numeric_limits<size_t>::max();
  }
}

void App::SwitchToPresentation(const size_t presentation_idx) {
  FXL_DCHECK(presentation_idx < presentations_.size());
  if (presentation_idx == active_presentation_idx_) {
    return;
  }
  active_presentation_idx_ = presentation_idx;
  layer_stack_->RemoveAllLayers();
  layer_stack_->AddLayer(presentations_[presentation_idx]->layer());

  if (magnifier_) {
    presentations_[presentation_idx]->RegisterWithMagnifier(magnifier_.get());
  }

  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void App::SwitchToNextPresentation() {
  SwitchToPresentation((active_presentation_idx_ + 1) % presentations_.size());
}

void App::SwitchToPreviousPresentation() {
  SwitchToPresentation((active_presentation_idx_ + presentations_.size() - 1) %
                       presentations_.size());
}

void App::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FXL_VLOG(1) << "RegisterDevice " << device_id << " " << descriptor;
  std::unique_ptr<ui_input::InputDeviceImpl> input_device =
      std::make_unique<ui_input::InputDeviceImpl>(device_id, std::move(descriptor),
                                                  std::move(input_device_request), this);

  // Media button processing is done exclusively in root_presenter::App.
  // Dependent components inside presentations register with the handler (passed
  // at construction) to receive such events.
  if (!media_buttons_handler_.OnDeviceAdded(input_device.get())) {
    for (auto& presentation : presentations_) {
      presentation->OnDeviceAdded(input_device.get());
    }
  }

  devices_by_id_.emplace(device_id, std::move(input_device));
}

void App::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FXL_VLOG(1) << "UnregisterDevice " << input_device->id();

  if (!media_buttons_handler_.OnDeviceRemoved(input_device->id())) {
    for (auto& presentation : presentations_) {
      presentation->OnDeviceRemoved(input_device->id());
    }
  }

  devices_by_id_.erase(input_device->id());
}

void App::OnReport(ui_input::InputDeviceImpl* input_device,
                   fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "root_presenter_on_report", "id", report.trace_id);
  TRACE_FLOW_END("input", "report_to_presenter", report.trace_id);

  FXL_VLOG(3) << "OnReport from " << input_device->id() << " " << report;

  if (devices_by_id_.count(input_device->id()) == 0) {
    return;
  }

  // TODO(fxb/36217): Do not clone once presentation stops needing input.
  fuchsia::ui::input::InputReport cloned_report;
  report.Clone(&cloned_report);

  if (report.media_buttons) {
    media_buttons_handler_.OnReport(input_device->id(), std::move(cloned_report));
    fdr_manager_->OnMediaButtonReport(*(report.media_buttons.get()));
    return;
  }

  if (presentations_.size() == 0) {
    return;
  }

  FXL_DCHECK(active_presentation_idx_ < presentations_.size());
  FXL_VLOG(3) << "OnReport to " << active_presentation_idx_;

  // Input events are only reported to the active presentation.
  TRACE_FLOW_BEGIN("input", "report_to_presentation", report.trace_id);
  presentations_[active_presentation_idx_]->OnReport(input_device->id(), std::move(report));
}

void App::InitializeServices() {
  if (!scenic_) {
    component_context_->svc()->Connect(scenic_.NewRequest());
    scenic_.set_error_handler([this](zx_status_t error) {
      FXL_LOG(ERROR) << "Scenic died, destroying all presentations.";
      Reset();
    });

    session_ = std::make_unique<scenic::Session>(scenic_.get());
    session_->set_error_handler([this](zx_status_t error) {
      FXL_LOG(ERROR) << "Session died, destroying all presentations.";
      Reset();
    });
    session_->set_event_handler([this](std::vector<fuchsia::ui::scenic::Event> events) {
      for (auto& event : events) {
        HandleScenicEvent(event);
      }
    });
    // Globally disable parallel dispatch of input events.
    // TODO(SCN-1047): Enable parallel dispatch.
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
    session_->Present(0, [](fuchsia::images::PresentationInfo info) {});

    scenic_->GetDisplayOwnershipEvent(
        [this](zx::event event) { input_reader_.SetOwnershipEvent(std::move(event)); });

    component_context_->svc()->Connect(magnifier_.NewRequest());
    // No need to set an error handler here unless we want to attempt a reconnect or something;
    // instead, we add error handlers for cleanup on the a11y presentations when we register them.

    component_context_->svc()->Connect(ime_service_.NewRequest());
    ime_service_.set_error_handler([this](zx_status_t error) {
      FXL_LOG(ERROR) << "IME Service died, destroying all presentations.";
      Reset();
    });

    component_context_->svc()->Connect(shortcut_manager_.NewRequest());
    shortcut_manager_.set_error_handler([this](zx_status_t error) {
      FXL_LOG(ERROR) << "Shortcut manager unavailable: " << zx_status_get_string(error);
      shortcut_manager_ = nullptr;
      for (const auto& presentation : presentations_) {
        presentation->ResetShortcutManager();
      }
    });

    // Add Accessibility Settings Watcher.
    // When multiple compositors are supported in future, following code need
    // to be updated to support that change.
    a11y_settings_watchers_ = std::make_unique<A11ySettingsWatcher>(
        *component_context_.get(), compositor_->id(), session_.get());
  }
}

void App::Reset() {
  presentations_.clear();           // must be first, holds pointers to services
  a11y_settings_watchers_.reset();  // session_ ptr may not be valid
  active_presentation_idx_ = std::numeric_limits<size_t>::max();
  layer_stack_ = nullptr;
  compositor_ = nullptr;
  session_ = nullptr;
  shortcut_manager_ = nullptr;
  scenic_.Unbind();
}

void App::HandleScenicEvent(const fuchsia::ui::scenic::Event& event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          auto& evt = event.gfx().view_disconnected();

          size_t idx = 0;
          for (idx = 0; idx < presentations_.size(); ++idx) {
            if (evt.view_holder_id == presentations_[idx]->view_holder().id()) {
              break;
            }
          }
          FXL_DCHECK(idx != presentations_.size());

          FXL_LOG(INFO) << "Root presenter: Content view terminated.";
          ShutdownPresentation(idx);
          break;
        }
        default: {
          break;
        }
      }
    default: {
      break;
    }
  }
}

void App::Register(fidl::InterfaceHandle<fuchsia::ui::input::accessibility::PointerEventListener>
                       pointer_event_listener) {
  if (!pointer_event_registry_) {
    component_context_->svc()->Connect(pointer_event_registry_.NewRequest());
  }
  FXL_LOG(INFO) << "Connecting to pointer event registry.";
  // Forward the listener to the registry we're connected to.
  auto callback = [](bool) {};
  pointer_event_registry_->Register(std::move(pointer_event_listener), std::move(callback));
}
}  // namespace root_presenter
