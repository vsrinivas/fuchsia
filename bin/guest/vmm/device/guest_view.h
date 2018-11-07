// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/ui/base_view/cpp/v1_base_view.h>

#include "garnet/bin/guest/vmm/device/gpu_scanout.h"

class GuestView : public scenic::V1BaseView {
 public:
  GuestView(
      scenic::ViewContext view_context,
      fidl::InterfaceHandle<fuchsia::guest::device::ViewListener> view_listener,
      fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener,
      GpuScanout* scanout);

 private:
  scenic::ShapeNode background_node_;
  scenic::Material material_;
  fuchsia::images::ImageInfo image_info_;
  std::unique_ptr<scenic::Memory> memory_;
  uint32_t scanout_source_width_;
  uint32_t scanout_source_height_;

  GpuScanout& scanout_;
  fuchsia::guest::device::ViewListenerPtr view_listener_;

  // |scenic::V1BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_
