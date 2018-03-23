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
#include <fuchsia/cpp/input.h>
#include "lib/ui/input/input_device_impl.h"
#include <fuchsia/cpp/presentation.h>
#include <fuchsia/cpp/views_v1.h>

namespace root_presenter {

class Presentation;

// The presenter provides a |presentation::Presenter| service which displays
// UI by attaching the provided view to the root of a new view tree
// associated with a new renderer.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public presentation::Presenter,
            public input::InputDeviceRegistry,
            public mozart::InputDeviceImpl::Listener {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) override;
  void OnReport(mozart::InputDeviceImpl* input_device,
                input::InputReport report) override;

 private:
  // |Presenter|:
  void Present(fidl::InterfaceHandle<views_v1_token::ViewOwner> view_owner,
               fidl::InterfaceRequest<presentation::Presentation>
                   presentation_request) override;

  // |InputDeviceRegistry|:
  void RegisterDevice(input::DeviceDescriptor descriptor,
                      fidl::InterfaceRequest<input::InputDevice>
                          input_device_request) override;

  void InitializeServices();
  void Reset();

  std::unique_ptr<component::ApplicationContext> application_context_;
  fidl::BindingSet<presentation::Presenter> presenter_bindings_;
  fidl::BindingSet<input::InputDeviceRegistry> input_receiver_bindings_;
  mozart::InputReader input_reader_;

  views_v1::ViewManagerPtr view_manager_;
  ui::ScenicPtr scenic_;

  std::vector<std::unique_ptr<Presentation>> presentations_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<mozart::InputDeviceImpl>>
      devices_by_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_APP_H_
