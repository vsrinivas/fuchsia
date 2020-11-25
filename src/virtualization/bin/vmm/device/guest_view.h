// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GUEST_VIEW_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GUEST_VIEW_H_

#include <fuchsia/virtualization/hardware/cpp/fidl.h>

#include "src/lib/ui/base_view/base_view.h"
#include "src/virtualization/bin/vmm/device/gpu_scanout.h"

class GuestView : public scenic::BaseView {
 public:
  GuestView(scenic::ViewContext view_context, GpuScanout* scanout,
            fuchsia::virtualization::hardware::ViewListenerPtr view_listener);

 private:
  scenic::ShapeNode background_;
  scenic::Material material_;
  fuchsia::images::ImageInfo image_info_;
  std::unique_ptr<scenic::Memory> memory_;
  uint32_t scanout_source_width_;
  uint32_t scanout_source_height_;

  GpuScanout& scanout_;
  fuchsia::virtualization::hardware::ViewListenerPtr view_listener_;

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_GUEST_VIEW_H_
