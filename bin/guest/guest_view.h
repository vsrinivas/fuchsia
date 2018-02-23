// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_GUEST_VIEW_H_

#include <zircon/types.h>

#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ui/scenic/client/host_memory.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"

class GuestView;

class ScenicScanout : public machina::GpuScanout, public mozart::ViewProvider {
 public:
  static zx_status_t Create(app::ApplicationContext* application_context,
                            machina::InputDispatcher* input_dispatcher,
                            fbl::unique_ptr<GpuScanout>* out);

  ScenicScanout(app::ApplicationContext* application_context,
                machina::InputDispatcher* input_dispatcher);

  // |GpuScanout|
  void FlushRegion(const virtio_gpu_rect_t& rect) override;

  // |ViewProvider|
  void CreateView(
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      f1dl::InterfaceRequest<app::ServiceProvider> view_services) override;

 private:
  machina::InputDispatcher* input_dispatcher_;
  app::ApplicationContext* application_context_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  f1dl::BindingSet<ViewProvider> bindings_;
  fbl::unique_ptr<GuestView> view_;
};

class GuestView : public mozart::BaseView {
 public:
  GuestView(machina::GpuScanout* scanout,
            machina::InputDispatcher* input_dispatcher,
            mozart::ViewManagerPtr view_manager,
            f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~GuestView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      ui_mozart::PresentationInfoPtr presentation_info) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::Material material_;
  scenic::ImageInfo image_info_;
  fbl::unique_ptr<scenic_lib::HostMemory> memory_;

  machina::InputDispatcher* input_dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestView);
};

#endif  // GARNET_BIN_GUEST_GUEST_VIEW_H_
