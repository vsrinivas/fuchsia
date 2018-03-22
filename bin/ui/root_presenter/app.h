// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_APP_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_APP_H_

#include <memory>
#include <vector>

#include "garnet/bin/ui/input_reader/input_reader.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/presentation/fidl/presenter.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"

namespace root_presenter {

class Presentation;

// The presenter provides a |mozart::Presenter| service which displays
// UI by attaching the provided view to the root of a new view tree
// associated with a new renderer.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public mozart::Presenter,
            public mozart::InputDeviceRegistry,
            public mozart::InputDeviceImpl::Listener {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) override;
  void OnReport(mozart::InputDeviceImpl* input_device,
                mozart::InputReportPtr report) override;

 private:
  // |Presenter|:
  void Present(f1dl::InterfaceHandle<mozart::ViewOwner> view_owner,
               f1dl::InterfaceRequest<mozart::Presentation>
                   presentation_request) override;

  // |InputDeviceRegistry|:
  void RegisterDevice(mozart::DeviceDescriptorPtr descriptor,
                      f1dl::InterfaceRequest<mozart::InputDevice>
                          input_device_request) override;

  void InitializeServices();
  void Reset();

  std::unique_ptr<component::ApplicationContext> application_context_;
  f1dl::BindingSet<mozart::Presenter> presenter_bindings_;
  f1dl::BindingSet<mozart::InputDeviceRegistry> input_receiver_bindings_;
  mozart::input::InputReader input_reader_;

  mozart::ViewManagerPtr view_manager_;
  ui::ScenicPtr scenic_;

  std::vector<std::unique_ptr<Presentation>> presentations_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<mozart::InputDeviceImpl>>
      devices_by_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_APP_H_
