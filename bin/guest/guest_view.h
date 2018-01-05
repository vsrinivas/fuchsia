// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_GUEST_VIEW_H_

#include <zircon/types.h>

#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ui/scenic/client/host_memory.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

class GuestView;

class ScenicScanout : public machina::GpuScanout {
 public:
  ScenicScanout(GuestView* view);
  void FlushRegion(const virtio_gpu_rect_t& rect) override;

 private:
  GuestView* view_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

class GuestView : public mozart::BaseView {
 public:
  static zx_status_t Start(machina::VirtioGpu*, machina::InputDispatcher*);

  GuestView(machina::VirtioGpu* gpu,
            machina::InputDispatcher* input_dispatcher,
            mozart::ViewManagerPtr view_manager,
            fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~GuestView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::Material material_;
  scenic::ImageInfo image_info_;
  fbl::unique_ptr<scenic_lib::HostMemory> memory_;

  ScenicScanout scanout_;
  machina::InputDispatcher* input_dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestView);
};

#endif  // GARNET_BIN_GUEST_GUEST_VIEW_H_
