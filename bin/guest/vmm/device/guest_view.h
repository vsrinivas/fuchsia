// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/ui/view_framework/base_view.h>

#include "garnet/bin/guest/vmm/device/gpu_scanout.h"

class GuestView : public mozart::BaseView {
 public:
  GuestView(
      GpuScanout* scanout,
      fidl::InterfaceHandle<fuchsia::ui::input::InputListener> input_listener,
      fidl::InterfaceHandle<fuchsia::guest::device::ViewListener> view_listener,
      fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request);

 private:
  scenic::ShapeNode background_node_;
  scenic::Material material_;
  fuchsia::images::ImageInfo image_info_;
  std::unique_ptr<scenic::Memory> memory_;
  uint32_t scanout_source_width_;
  uint32_t scanout_source_height_;

  GpuScanout& scanout_;
  fuchsia::guest::device::ViewListenerPtr view_listener_;

  // |mozart::BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_GUEST_VIEW_H_
