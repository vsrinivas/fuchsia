// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_
#define APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/presentation/fidl/presenter.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "garnet/bin/ui/input_reader/input_reader.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

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
  explicit App(const ftl::CommandLine& command_line);
  ~App();

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) override;
  void OnReport(mozart::InputDeviceImpl* input_device,
                mozart::InputReportPtr report) override;

 private:
  // |Presenter|:
  void Present(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) override;

  // |InputDeviceRegistry|:
  void RegisterDevice(mozart::DeviceDescriptorPtr descriptor,
                      fidl::InterfaceRequest<mozart::InputDevice>
                          input_device_request) override;

  void InitializeServices();
  void Reset();

  std::unique_ptr<app::ApplicationContext> application_context_;
  fidl::BindingSet<mozart::Presenter> presenter_bindings_;
  fidl::BindingSet<mozart::InputDeviceRegistry> input_receiver_bindings_;
  mozart::input::InputReader input_reader_;

  mozart::ViewManagerPtr view_manager_;
  scenic::SceneManagerPtr scene_manager_;

  std::vector<std::unique_ptr<Presentation>> presentations_;

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<mozart::InputDeviceImpl>>
      devices_by_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_
