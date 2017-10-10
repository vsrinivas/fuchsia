// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/app.h"

#include <algorithm>

#include "garnet/bin/ui/root_presenter/presentation.h"
#include "lib/app/cpp/connect.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"

namespace root_presenter {

App::App(const fxl::CommandLine& command_line)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      input_reader_(this) {
  FXL_DCHECK(application_context_);

  input_reader_.Start();

  application_context_->outgoing_services()->AddService<mozart::Presenter>(
      [this](fidl::InterfaceRequest<mozart::Presenter> request) {
        presenter_bindings_.AddBinding(this, std::move(request));
      });

  application_context_->outgoing_services()
      ->AddService<mozart::InputDeviceRegistry>(
          [this](fidl::InterfaceRequest<mozart::InputDeviceRegistry> request) {
            input_receiver_bindings_.AddBinding(this, std::move(request));
          });
}

App::~App() {}

void App::Present(
    fidl::InterfaceHandle<mozart::ViewOwner> view_owner_handle,
    fidl::InterfaceRequest<mozart::Presentation> presentation_request) {
  InitializeServices();

  auto presentation =
      std::make_unique<Presentation>(view_manager_.get(), scene_manager_.get());
  presentation->Present(
      mozart::ViewOwnerPtr::Create(std::move(view_owner_handle)),
      std::move(presentation_request),
      [ this, presentation = presentation.get() ] {
        auto it = std::find_if(
            presentations_.begin(), presentations_.end(),
            [presentation](const std::unique_ptr<Presentation>& other) {
              return other.get() == presentation;
            });
        FXL_DCHECK(it != presentations_.end());
        presentations_.erase(it);
      });

  for (auto& it : devices_by_id_) {
    presentation->OnDeviceAdded(it.second.get());
  }
  presentations_.push_back(std::move(presentation));
}

void App::RegisterDevice(
    mozart::DeviceDescriptorPtr descriptor,
    fidl::InterfaceRequest<mozart::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FXL_VLOG(1) << "RegisterDevice " << device_id << " " << *descriptor;
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
                   mozart::InputReportPtr report) {
  FXL_VLOG(2) << "OnReport from " << input_device->id() << " " << *report;
  if (devices_by_id_.count(input_device->id()) == 0)
    return;

  FXL_VLOG(2) << "OnReport to " << presentations_.size();
  for (auto& presentation : presentations_) {
    presentation->OnReport(input_device->id(), report.Clone());
  }
}

void App::InitializeServices() {
  if (!view_manager_) {
    application_context_->ConnectToEnvironmentService(
        view_manager_.NewRequest());
    view_manager_.set_connection_error_handler([this] {
      FXL_LOG(ERROR) << "ViewManager died, destroying view trees.";
      Reset();
    });

    view_manager_->GetSceneManager(scene_manager_.NewRequest());
    scene_manager_.set_connection_error_handler([this] {
      FXL_LOG(ERROR) << "SceneManager died, destroying view trees.";
      Reset();
    });
  }
}

void App::Reset() {
  presentations_.clear();  // must be first, holds pointers to services
  view_manager_.reset();
  scene_manager_.reset();
}

}  // namespace root_presenter
