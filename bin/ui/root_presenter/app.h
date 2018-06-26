// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_APP_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_APP_H_

#include <limits>
#include <memory>
#include <vector>

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "garnet/bin/ui/input_reader/input_reader.h"
#include "garnet/bin/ui/root_presenter/presentation_new.h"
#include "garnet/bin/ui/root_presenter/presentation_old.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/scenic/client/resources.h"

namespace root_presenter {

class PresentationOld;

// The presenter provides a |fuchsia::ui::policy::Presenter| service which
// displays UI by attaching the provided view to the root of a new view tree
// associated with a new renderer.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public fuchsia::ui::policy::Presenter,
            public fuchsia::ui::policy::Presenter2,
            public fuchsia::ui::input::InputDeviceRegistry,
            public mozart::InputDeviceImpl::Listener {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

  // |InputDeviceImpl::Listener|
  void OnDeviceDisconnected(mozart::InputDeviceImpl* input_device) override;
  void OnReport(mozart::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

 private:
  // |Presenter|
  void Present(fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner>
                   view_owner,
               fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                   presentation_request) override;

  // |Presenter|
  void HACK_SetRendererParams(
      bool enable_clipping,
      ::fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> params) override;

  // |Presenter2|
  void PresentView(zx::eventpair view_holder_token,
                   ::fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                       presentation_request) override;

  // |InputDeviceRegistry|
  void RegisterDevice(fuchsia::ui::input::DeviceDescriptor descriptor,
                      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                          input_device_request) override;

  void InitializeServices();
  void Reset();

  void AddPresentation(std::unique_ptr<Presentation> presentation);
  void SwitchToPresentation(size_t presentation_idx);
  void SwitchToNextPresentation();
  void SwitchToPreviousPresentation();

  Presentation::YieldCallback GetYieldCallback();
  Presentation::ShutdownCallback GetShutdownCallback(
      Presentation* presentation);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fidl::BindingSet<fuchsia::ui::policy::Presenter> presenter_bindings_;
  fidl::BindingSet<fuchsia::ui::policy::Presenter2> presenter2_bindings_;
  fidl::BindingSet<fuchsia::ui::input::InputDeviceRegistry>
      input_receiver_bindings_;
  mozart::InputReader input_reader_;

  ::fuchsia::ui::views_v1::ViewManagerPtr view_manager_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::unique_ptr<scenic_lib::Session> session_;
  std::unique_ptr<scenic_lib::DisplayCompositor> compositor_;
  std::unique_ptr<scenic_lib::LayerStack> layer_stack_;

  RendererParams renderer_params_;
  std::vector<std::unique_ptr<Presentation>> presentations_;
  // A valid index into presentations_, otherwise size_t::max().
  size_t active_presentation_idx_ = std::numeric_limits<size_t>::max();

  uint32_t next_device_token_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<mozart::InputDeviceImpl>>
      devices_by_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_APP_H_
