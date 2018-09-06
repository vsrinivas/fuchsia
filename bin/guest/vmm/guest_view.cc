// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/guest_view.h"

#include <semaphore.h>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

// static
zx_status_t ScenicScanout::Create(component::StartupContext* startup_context,
                                  machina::InputDispatcherImpl* input_dispatcher,
                                  fbl::unique_ptr<ScenicScanout>* out) {
  *out = fbl::make_unique<ScenicScanout>(startup_context, input_dispatcher);
  return ZX_OK;
}

ScenicScanout::ScenicScanout(component::StartupContext* startup_context,
                             machina::InputDispatcherImpl* input_dispatcher)
    : input_dispatcher_(input_dispatcher), startup_context_(startup_context) {
  // The actual framebuffer can't be created until we've connected to the
  // mozart service.
  SetReady(false);

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
  view_ = fbl::make_unique<GuestView>(this, input_dispatcher_,
                                      std::move(view_manager),
                                      std::move(view_owner_request));
  if (view_) {
    view_->SetReleaseHandler([this] { view_.reset(); });
  }
  SetReady(true);
}

void ScenicScanout::InvalidateRegion(const machina::GpuRect& rect) {
  async::PostTask(async_get_default_dispatcher(),
                  [this] { view_->InvalidateScene(); });
}

GuestView::GuestView(
    machina::GpuScanout* scanout, machina::InputDispatcherImpl* input_dispatcher,
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Guest"),
      background_node_(session()),
      material_(session()),
      input_dispatcher_(input_dispatcher) {
  background_node_.SetMaterial(material_);
  parent_node().AddChild(background_node_);

  image_info_.width = kGuestViewDisplayWidth;
  image_info_.height = kGuestViewDisplayHeight;
  image_info_.stride = kGuestViewDisplayWidth * 4;
  image_info_.pixel_format = fuchsia::images::PixelFormat::BGRA_8;

  // Allocate a framebuffer and attach it as a GPU scanout.
  memory_ = fbl::make_unique<scenic::HostMemory>(
      session(), scenic::Image::ComputeSize(image_info_));
  machina::GpuBitmap bitmap(kGuestViewDisplayWidth, kGuestViewDisplayHeight,
                            ZX_PIXEL_FORMAT_ARGB_8888,
                            reinterpret_cast<uint8_t*>(memory_->data_ptr()));
  scanout->SetBitmap(std::move(bitmap));
}

GuestView::~GuestView() = default;

void GuestView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  const uint32_t width = logical_size().width;
  const uint32_t height = logical_size().height;
  scenic::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);

  static constexpr float kBackgroundElevation = 0.f;
  const float center_x = width * .5f;
  const float center_y = height * .5f;
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);

  scenic::HostImage image(*memory_, 0u, image_info_);
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
  }
  input_dispatcher_->DispatchEvent(std::move(event));
  return false;
}
