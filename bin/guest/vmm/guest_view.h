// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_

#include <lib/async/dispatcher.h>
#include <zircon/types.h>

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/input_dispatcher_impl.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ui/scenic/cpp/host_memory.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/view_framework/base_view.h"

class GuestView;

class ScenicScanout : public ::fuchsia::ui::viewsv1::ViewProvider {
 public:
  ScenicScanout(component::StartupContext* startup_context,
                fuchsia::ui::input::InputDispatcherPtr input_dispatcher,
                machina::GpuScanout* scanout);

  // |ViewProvider|
  void CreateView(fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
                      view_owner_request,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                      view_services) override;

 private:
  machina::GpuScanout* scanout_;
  fuchsia::ui::input::InputDispatcherPtr input_dispatcher_;
  component::StartupContext* startup_context_;
  fidl::BindingSet<ViewProvider> bindings_;
  std::unique_ptr<GuestView> view_;
};

class GuestView : public mozart::BaseView {
 public:
  GuestView(machina::GpuScanout* scanout,
            fuchsia::ui::input::InputDispatcherPtr input_dispatcher,
            ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
            fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
                view_owner_request);

  ~GuestView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
  bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  scenic::ShapeNode background_node_;
  scenic::Material material_;
  fuchsia::images::ImageInfo image_info_;
  std::unique_ptr<scenic::Memory> memory_;
  uint32_t scanout_source_width_;
  uint32_t scanout_source_height_;

  machina::GpuScanout* scanout_;
  fuchsia::ui::input::InputDispatcherPtr input_dispatcher_;

  bool view_ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestView);
};

#endif  // GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
