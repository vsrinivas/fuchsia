// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/guest_view.h"

#include <semaphore.h>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/images/cpp/images.h>

ScenicScanout::ScenicScanout(
    component::StartupContext* startup_context,
    fuchsia::ui::input::InputDispatcherPtr input_dispatcher,
    machina::GpuScanout* scanout)
    : scanout_(scanout),
      input_dispatcher_(std::move(input_dispatcher)),
      startup_context_(startup_context) {
  startup_context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void ScenicScanout::CreateView(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> view_services) {
  if (view_) {
    FXL_LOG(ERROR) << "CreateView called when a view already exists";
    return;
  }
  auto view_manager =
      startup_context_
          ->ConnectToEnvironmentService<::fuchsia::ui::viewsv1::ViewManager>();
  view_ = std::make_unique<GuestView>(scanout_, std::move(input_dispatcher_),
                                      std::move(view_manager),
                                      std::move(view_owner_request));
  view_->SetReleaseHandler([this] { view_.reset(); });
}

GuestView::GuestView(
    machina::GpuScanout* scanout,
    fuchsia::ui::input::InputDispatcherPtr input_dispatcher,
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Guest"),
      background_node_(session()),
      material_(session()),
      scanout_(scanout),
      input_dispatcher_(std::move(input_dispatcher)) {
  background_node_.SetMaterial(material_);
  parent_node().AddChild(background_node_);

  scanout_->SetFlushHandler(
      [this](virtio_gpu_rect_t rect) { InvalidateScene(); });

  scanout_->SetUpdateSourceHandler([this](uint32_t width, uint32_t height) {
    scanout_source_width_ = width;
    scanout_source_height_ = height;
    InvalidateScene();
  });
}

GuestView::~GuestView() = default;

void GuestView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || !has_physical_size()) {
    return;
  }

  if (physical_size().width != image_info_.width ||
      physical_size().height != image_info_.height) {
    image_info_.width = physical_size().width;
    image_info_.height = physical_size().height;
    image_info_.stride = image_info_.width * 4;
    image_info_.pixel_format = fuchsia::images::PixelFormat::BGRA_8;

    // Allocate a framebuffer and attach it as a GPU scanout.
    zx::vmo scanout_vmo;
    auto vmo_size = images::ImageSize(image_info_);
    zx_status_t status = zx::vmo::create(vmo_size, 0, &scanout_vmo);
    FXL_CHECK(status == ZX_OK)
        << "Scanout target VMO creation failed " << status;
    zx::vmo scenic_vmo;
    status = scanout_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &scenic_vmo);
    FXL_CHECK(status == ZX_OK)
        << "Scanout target VMO duplication failed " << status;
    memory_ = std::make_unique<scenic::Memory>(
        session(), std::move(scenic_vmo), vmo_size,
        fuchsia::images::MemoryType::HOST_MEMORY);

    scanout_->SetFlushTarget(std::move(scanout_vmo), vmo_size,
                             image_info_.width, image_info_.height,
                             image_info_.stride);
  }

  const float width = logical_size().width;
  const float height = logical_size().height;
  scenic::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);
  static constexpr float kBackgroundElevation = 0.f;
  const float center_x = width * .5f;
  const float center_y = height * .5f;
  const float scale_x =
      static_cast<float>(image_info_.width) / scanout_source_width_;
  const float scale_y =
      static_cast<float>(image_info_.height) / scanout_source_height_;

  // Scale the background node such that the scanout resource sub-region
  // matches the image size. Ideally, this would just be a scale transform of
  // the material itself.
  // TODO(SCN-958): Materials should support transforms
  background_node_.SetAnchor(-center_x, -center_y, 0.0f);
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);
  background_node_.SetScale(scale_x, scale_y, 1.0f);

  scenic::Image image(*memory_, 0u, image_info_);
  material_.SetTexture(image);

  view_ready_ = true;
}

bool GuestView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (event.is_pointer()) {
    if (!view_ready_) {
      // Ignore pointer events that come in before the view is ready.
      return true;
    }

    // Normalize pointer positions to 0..1.
    // TODO(SCN-921): pointer event positions outside view boundaries.
    event.pointer().x /= logical_size().width;
    event.pointer().y /= logical_size().height;

    // Override the pointer type to touch because the view event positions are
    // always absolute.
    event.pointer().type = fuchsia::ui::input::PointerEventType::TOUCH;

    // Ignore unsupported event phases. Note that these are opt-out so that if
    // new phases are added, VirtioInput will log a warning message.
    switch (event.pointer().phase) {
      case fuchsia::ui::input::PointerEventPhase::ADD:
      case fuchsia::ui::input::PointerEventPhase::HOVER:
      case fuchsia::ui::input::PointerEventPhase::REMOVE:
      case fuchsia::ui::input::PointerEventPhase::CANCEL:
        return true;
      default:
        break;
    }
  }
  input_dispatcher_->DispatchEvent(std::move(event));
  return false;
}
