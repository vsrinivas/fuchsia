// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/app.h"

#include <algorithm>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "lib/component/cpp/connect.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace root_presenter {

App::App(const fxl::CommandLine& command_line)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      input_reader_(this) {
  FXL_DCHECK(startup_context_);

  input_reader_.Start();

  startup_context_->outgoing().AddPublicService(
      presenter_bindings_.GetHandler(this));
  startup_context_->outgoing().AddPublicService(
      presenter2_bindings_.GetHandler(this));
  startup_context_->outgoing().AddPublicService(
      input_receiver_bindings_.GetHandler(this));
}

App::~App() {}

Presentation::YieldCallback App::GetYieldCallback() {
  return fxl::MakeCopyable([this](bool yield_to_next) {
    if (yield_to_next) {
      SwitchToNextPresentation();
    } else {
      SwitchToPreviousPresentation();
    }
  });
}

Presentation::ShutdownCallback App::GetShutdownCallback(
    Presentation* presentation) {
  return fxl::MakeCopyable([this, presentation] {
    size_t idx;
    for (idx = 0; idx < presentations_.size(); ++idx) {
      if (presentations_[idx].get() == presentation) {
        break;
      }
    }
    FXL_DCHECK(idx != presentations_.size());

    if (idx == active_presentation_idx_) {
      // This works fine when idx == 0, because the previous idx chosen will
      // also be 0, and it will be an no-op within SwitchToPreviousPresentation.
      // Finally, at the end of the callback, everything will be cleaned up.
      SwitchToPreviousPresentation();
    }

    presentations_.erase(presentations_.begin() + idx);
    if (idx < active_presentation_idx_) {
      // Adjust index into presentations_.
      active_presentation_idx_--;
    }

    if (presentations_.empty()) {
      layer_stack_->RemoveAllLayers();
      active_presentation_idx_ = std::numeric_limits<size_t>::max();
    }
  });
}

void App::Present(
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_handle,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
        presentation_request) {
  InitializeServices();

  auto presentation = std::make_unique<Presentation1>(
      view_manager_.get(), scenic_.get(), session_.get(), renderer_params_);
  presentation->Present(view_owner_handle.Bind(),
                        std::move(presentation_request), GetYieldCallback(),
                        GetShutdownCallback(presentation.get()));

  AddPresentation(std::move(presentation));
}

void App::PresentView(
    zx::eventpair view_holder_token,
    ::fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
        presentation_request) {
  InitializeServices();

  auto presentation = std::make_unique<Presentation2>(
      scenic_.get(), session_.get(), std::move(view_holder_token),
      renderer_params_);
  presentation->PresentView(std::move(presentation_request), GetYieldCallback(),
                            GetShutdownCallback(presentation.get()));
  AddPresentation(std::move(presentation));
}

void App::AddPresentation(std::unique_ptr<Presentation> presentation) {
  for (auto& it : devices_by_id_) {
    presentation->OnDeviceAdded(it.second.get());
  }

  presentations_.push_back(std::move(presentation));
  SwitchToPresentation(presentations_.size() - 1);
}

void App::HACK_SetRendererParams(
    bool enable_clipping,
    ::fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> params) {
  renderer_params_.clipping_enabled.set_value(enable_clipping);
  FXL_LOG(INFO)
      << "Presenter::HACK_SetRendererParams: Setting clipping enabled to "
      << (enable_clipping ? "true" : "false");
  for (auto& param : *params) {
    switch (param.Which()) {
      case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
        renderer_params_.shadow_technique.set_value(param.shadow_technique());
        FXL_LOG(INFO)
            << "Presenter::HACK_SetRendererParams: Setting shadow technique to "
            << fidl::ToUnderlying(param.shadow_technique());
        continue;
      case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
        renderer_params_.render_frequency.set_value(param.render_frequency());
        FXL_LOG(INFO)
            << "Presenter::HACK_SetRendererParams: Setting render frequency to "
            << fidl::ToUnderlying(param.render_frequency());
        continue;
      case fuchsia::ui::gfx::RendererParam::Tag::Invalid:
        continue;
    }
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
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void App::SwitchToNextPresentation() {
  SwitchToPresentation((active_presentation_idx_ + 1) % presentations_.size());
}

void App::SwitchToPreviousPresentation() {
  SwitchToPresentation((active_presentation_idx_ + presentations_.size() - 1) %
                       presentations_.size());
}

void App::RegisterDevice(fuchsia::ui::input::DeviceDescriptor descriptor,
                         fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                             input_device_request) {
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
                   fuchsia::ui::input::InputReport report) {
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
    startup_context_->ConnectToEnvironmentService(view_manager_.NewRequest());
    view_manager_.set_error_handler([this] {
      FXL_LOG(ERROR) << "ViewManager died, destroying view trees.";
      Reset();
    });

    view_manager_->GetScenic(scenic_.NewRequest());
    scenic_.set_error_handler([this] {
      FXL_LOG(ERROR) << "Scenic died, destroying view trees.";
      Reset();
    });

    session_ = std::make_unique<scenic::Session>(scenic_.get());
    session_->set_error_handler([this] {
      FXL_LOG(ERROR) << "Session died, destroying view trees.";
      Reset();
    });

    compositor_ = std::make_unique<scenic::DisplayCompositor>(session_.get());
    layer_stack_ = std::make_unique<scenic::LayerStack>(session_.get());
    compositor_->SetLayerStack(*layer_stack_.get());
    session_->Present(0, [](fuchsia::images::PresentationInfo info) {});

    scenic_->GetDisplayOwnershipEvent([this](zx::event event) {
      input_reader_.SetOwnershipEvent(std::move(event));
    });
  }
}

void App::Reset() {
  presentations_.clear();  // must be first, holds pointers to services
  active_presentation_idx_ = std::numeric_limits<size_t>::max();
  view_manager_.Unbind();
  layer_stack_ = nullptr;
  compositor_ = nullptr;
  session_ = nullptr;
  scenic_.Unbind();
}

}  // namespace root_presenter
