// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/app.h"

#include <algorithm>

#include <fuchsia/cpp/views_v1.h>
#include "garnet/bin/ui/root_presenter/presentation.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace root_presenter {

App::App(const fxl::CommandLine& command_line)
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()),
      input_reader_(this) {
  FXL_DCHECK(application_context_);

  input_reader_.Start();

  application_context_->outgoing_services()->AddService<presentation::Presenter>(
      [this](fidl::InterfaceRequest<presentation::Presenter> request) {
        presenter_bindings_.AddBinding(this, std::move(request));
      });

  application_context_->outgoing_services()
      ->AddService<input::InputDeviceRegistry>(
          [this](fidl::InterfaceRequest<input::InputDeviceRegistry> request) {
            input_receiver_bindings_.AddBinding(this, std::move(request));
          });
}

App::~App() {}

void App::Present(
    fidl::InterfaceHandle<views_v1_token::ViewOwner> view_owner_handle,
    fidl::InterfaceRequest<presentation::Presentation> presentation_request) {
  InitializeServices();

  auto presentation =
      std::make_unique<Presentation>(view_manager_.get(),
                                     scenic_.get(), session_.get());
  Presentation::YieldCallback yield_callback = [this](bool yield_to_next) {
    if (yield_to_next) {
      SwitchToNextPresentation();
    } else {
      SwitchToPreviousPresentation();
    }
  };
  Presentation::ShutdownCallback shutdown_callback =
      [this, presentation = presentation.get()] {
    size_t idx;
    for (idx = 0; idx < presentations_.size(); ++idx) {
      if (presentations_[idx].get() == presentation) {
        break;
      }
    }
    FXL_DCHECK(idx != presentations_.size());

    if (idx == active_presentation_idx_) {
      SwitchToPreviousPresentation();
    }

    presentations_.erase(presentations_.begin() + idx);
    if (idx < active_presentation_idx_) {
      active_presentation_idx_--;
    }

    if (presentations_.empty()) {
      layer_stack_->RemoveAllLayers();
    }
  };

  presentation->Present(
      view_owner_handle.Bind(), std::move(presentation_request),
      yield_callback, shutdown_callback);

  for (auto& it : devices_by_id_) {
    presentation->OnDeviceAdded(it.second.get());
  }

  presentations_.push_back(std::move(presentation));
  SwitchToPresentation(presentations_.size() - 1);
}

void App::SwitchToPresentation(const size_t presentation_idx) {
  FXL_DCHECK(presentation_idx < presentations_.size());
  if (presentation_idx == active_presentation_idx_) {
    return;
  }
  active_presentation_idx_ = presentation_idx;
  layer_stack_->RemoveAllLayers();
  layer_stack_->AddLayer(presentations_[presentation_idx]->layer());
  session_->Present(0, [](images::PresentationInfo info) {});
}

void App::SwitchToNextPresentation() {
  SwitchToPresentation(
      (active_presentation_idx_ + 1) % presentations_.size());
}

void App::SwitchToPreviousPresentation() {
  SwitchToPresentation(
      (active_presentation_idx_ + presentations_.size() - 1) %
          presentations_.size());
}

void App::RegisterDevice(
    input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<input::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FXL_VLOG(1) << "RegisterDevice " << device_id << " " << descriptor;
  std::unique_ptr<mozart::InputDeviceImpl> input_device =
      std::make_unique<mozart::InputDeviceImpl>(
          device_id, std::move(descriptor), std::move(input_device_request),
          this);

  for (auto& presentation : presentations_) {
    presentation->OnDeviceAdded(input_device.get());
  }

  devices_by_id_.emplace(device_id, std::move(input_device));
}

void App::OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) {
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FXL_VLOG(1) << "UnregisterDevice " << input_device->id();

  for (auto& presentation : presentations_) {
    presentation->OnDeviceRemoved(input_device->id());
  }
  devices_by_id_.erase(input_device->id());
}

void App::OnReport(mozart::InputDeviceImpl* input_device,
                   input::InputReport report) {
  FXL_VLOG(2) << "OnReport from " << input_device->id() << " " << report;
  if (devices_by_id_.count(input_device->id()) == 0 ||
      presentations_.size() == 0)
    return;

  FXL_DCHECK(active_presentation_idx_ < presentations_.size());
  FXL_VLOG(2) << "OnReport to " << active_presentation_idx_;

  // Input events are only reported to the active presentation.
  presentations_[active_presentation_idx_]->OnReport(input_device->id(),
                                                     std::move(report));
}

void App::InitializeServices() {
  if (!view_manager_) {
    application_context_->ConnectToEnvironmentService(
        view_manager_.NewRequest());
    view_manager_.set_error_handler([this] {
      FXL_LOG(ERROR) << "ViewManager died, destroying view trees.";
      Reset();
    });

    view_manager_->GetScenic(scenic_.NewRequest());
    scenic_.set_error_handler([this] {
      FXL_LOG(ERROR) << "Scenic died, destroying view trees.";
      Reset();
    });

    session_ = std::make_unique<scenic_lib::Session>(scenic_.get());
    session_->set_error_handler([this] {
      FXL_LOG(ERROR) << "Session died, destroying view trees.";
      Reset();
    });

    compositor_ = std::make_unique<scenic_lib::DisplayCompositor>(
        session_.get());
    layer_stack_ = std::make_unique<scenic_lib::LayerStack>(session_.get());
    compositor_->SetLayerStack(*layer_stack_.get());
    session_->Present(0, [](images::PresentationInfo info) {});
  }
}

void App::Reset() {
  presentations_.clear();  // must be first, holds pointers to services
  view_manager_.Unbind();
  layer_stack_ = nullptr;
  compositor_ = nullptr;
  session_ = nullptr;
  scenic_.Unbind();
}

}  // namespace root_presenter
