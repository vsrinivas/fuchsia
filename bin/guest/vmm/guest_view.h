// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_

#include <lib/async/dispatcher.h>
#include <zircon/types.h>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ui/scenic/cpp/host_memory.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/view_framework/base_view.h"

// For now we expose a fixed size display to the guest. Scenic will scale this
// buffer to the actual window size on the host.
// TODO(PD-109): Support resizing the display.
// TODO(PD-108): Support resizing the input to match the display.
static constexpr uint32_t kGuestViewDisplayWidth = 1024;
static constexpr uint32_t kGuestViewDisplayHeight = 768;

class GuestView;

class ScenicScanout : public machina::GpuScanout,
                      public ::fuchsia::ui::viewsv1::ViewProvider {
 public:
  static zx_status_t Create(component::StartupContext* startup_context,
                            machina::InputDispatcher* input_dispatcher,
                            fbl::unique_ptr<ScenicScanout>* out);

  ScenicScanout(component::StartupContext* startup_context,
                machina::InputDispatcher* input_dispatcher);

  // |GpuScanout|
  void InvalidateRegion(const machina::GpuRect& rect) override;

  // |ViewProvider|
  void CreateView(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> view_services)
      override;

 private:
  machina::InputDispatcher* input_dispatcher_;
  component::StartupContext* startup_context_;
  fidl::BindingSet<ViewProvider> bindings_;
  fbl::unique_ptr<GuestView> view_;
};

class GuestView : public mozart::BaseView {
 public:
  GuestView(machina::GpuScanout* scanout,
            machina::InputDispatcher* input_dispatcher,
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
  fbl::unique_ptr<scenic::HostMemory> memory_;

  machina::InputDispatcher* input_dispatcher_;

  float pointer_scale_x_ = 0.0f;
  float pointer_scale_y_ = 0.0f;
  bool view_ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestView);
};

#endif  // GARNET_BIN_GUEST_VMM_GUEST_VIEW_H_
