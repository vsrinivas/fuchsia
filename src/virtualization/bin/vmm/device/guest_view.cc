// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/guest_view.h"

#include <lib/images/cpp/images.h>
#include <lib/syslog/cpp/macros.h>

GuestView::GuestView(
    scenic::ViewContext view_context,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::ViewListener> view_listener,
    GpuScanout* scanout)
    : BaseView(std::move(view_context), "Guest"),
      background_(session()),
      material_(session()),
      scanout_(*scanout),
      view_listener_(view_listener.Bind()) {
  root_node().AddChild(background_);
  background_.SetMaterial(material_);

  // Request hard key events be delivered to the view.
  fuchsia::ui::input::Command command;
  command.set_set_hard_keyboard_delivery({.delivery_request = true});
  session()->Enqueue(std::move(command));

  scanout_.SetFlushHandler([this](virtio_gpu_rect_t rect) { InvalidateScene(); });
  scanout_.SetUpdateSourceHandler([this](uint32_t width, uint32_t height) {
    scanout_source_width_ = width;
    scanout_source_height_ = height;
    InvalidateScene();
  });
}

void GuestView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || !has_physical_size()) {
    return;
  }
  if (static_cast<uint32_t>(physical_size().x) != image_info_.width ||
      static_cast<uint32_t>(physical_size().y) != image_info_.height) {
    image_info_.width = physical_size().x;
    image_info_.height = physical_size().y;
    image_info_.stride = image_info_.width * 4;
    image_info_.pixel_format = fuchsia::images::PixelFormat::BGRA_8;

    // Allocate a framebuffer and attach it as a GPU scanout.
    zx::vmo scanout_vmo;
    auto vmo_size = images::ImageSize(image_info_);
    zx_status_t status = zx::vmo::create(vmo_size, 0, &scanout_vmo);
    FX_CHECK(status == ZX_OK) << "Scanout target VMO creation failed " << status;
    zx::vmo scenic_vmo;
    status = scanout_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &scenic_vmo);
    FX_CHECK(status == ZX_OK) << "Scanout target VMO duplication failed " << status;
    memory_ = std::make_unique<scenic::Memory>(session(), std::move(scenic_vmo), vmo_size,
                                               fuchsia::images::MemoryType::HOST_MEMORY);

    status = scanout_.SetFlushTarget(std::move(scanout_vmo), vmo_size, image_info_.width,
                                     image_info_.height, image_info_.stride);
    FX_CHECK(status == ZX_OK) << "Scanout target VMO flush failed " << status;
  }

  const float width = logical_size().x;
  const float height = logical_size().y;
  scenic::Rectangle shape(session(), width, height);
  background_.SetShape(shape);
  const float center_x = width * .5f;
  const float center_y = height * .5f;
  const float scale_x = static_cast<float>(image_info_.width) / scanout_source_width_;
  const float scale_y = static_cast<float>(image_info_.height) / scanout_source_height_;

  // Scale the background node such that the scanout resource sub-region
  // matches the image size. Ideally, this would just be a scale transform of
  // the material itself.
  // TODO(fxbug.dev/24174): Materials should support transforms
  background_.SetAnchor(-center_x, -center_y, 0.0f);
  background_.SetTranslation(center_x, center_y, 0.0f);
  background_.SetScale(scale_x, scale_y, 1.0f);

  scenic::Image image(*memory_, 0u, image_info_);
  material_.SetTexture(image);
}

void GuestView::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) {
  view_listener_->OnSizeChanged(logical_size());
}

void GuestView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  view_listener_->OnInputEvent(std::move(event));
}

void GuestView::OnScenicError(std::string error) {
  FX_LOGS(ERROR) << "Scenic session failed " << error;
}
