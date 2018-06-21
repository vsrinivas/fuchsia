// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/guest_view.h"

#include <semaphore.h>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

// static
zx_status_t ScenicScanout::Create(fuchsia::sys::StartupContext* startup_context,
                                  machina::InputDispatcher* input_dispatcher,
                                  fbl::unique_ptr<ScenicScanout>* out) {
  *out = fbl::make_unique<ScenicScanout>(startup_context, input_dispatcher);
  return ZX_OK;
}

ScenicScanout::ScenicScanout(fuchsia::sys::StartupContext* startup_context,
                             machina::InputDispatcher* input_dispatcher)
    : input_dispatcher_(input_dispatcher), startup_context_(startup_context) {
  // The actual framebuffer can't be created until we've connected to the
  // mozart service.
  SetReady(false);

  startup_context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void ScenicScanout::CreateView(
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
        view_owner_request,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> view_services) {
  if (view_) {
    FXL_LOG(ERROR) << "CreateView called when a view already exists";
    return;
  }
  auto view_manager =
      startup_context_
          ->ConnectToEnvironmentService<::fuchsia::ui::views_v1::ViewManager>();
  view_ = fbl::make_unique<GuestView>(this, input_dispatcher_,
                                      fbl::move(view_manager),
                                      fbl::move(view_owner_request));
  if (view_) {
    view_->SetReleaseHandler([this] { view_.reset(); });
  }
  SetReady(true);
}

void ScenicScanout::InvalidateRegion(const machina::GpuRect& rect) {
  async::PostTask(async_get_default(), [this] { view_->InvalidateScene(); });
}

GuestView::GuestView(
    machina::GpuScanout* scanout, machina::InputDispatcher* input_dispatcher,
    ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
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
  memory_ = fbl::make_unique<scenic_lib::HostMemory>(
      session(), scenic_lib::Image::ComputeSize(image_info_));
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
  scenic_lib::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);

  static constexpr float kBackgroundElevation = 0.f;
  const float center_x = width * .5f;
  const float center_y = height * .5f;
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);

  scenic_lib::HostImage image(*memory_, 0u, image_info_);
  material_.SetTexture(image);

  pointer_scale_x_ = static_cast<float>(kGuestViewDisplayWidth) / width;
  pointer_scale_y_ = static_cast<float>(kGuestViewDisplayHeight) / height;
  view_ready_ = true;
}

zx_status_t FromMozartButton(uint32_t event, machina::Button* button) {
  switch (event) {
    case fuchsia::ui::input::kMousePrimaryButton:
      *button = machina::Button::BTN_MOUSE_PRIMARY;
      return ZX_OK;
    case fuchsia::ui::input::kMouseSecondaryButton:
      *button = machina::Button::BTN_MOUSE_SECONDARY;
      return ZX_OK;
    case fuchsia::ui::input::kMouseTertiaryButton:
      *button = machina::Button::BTN_MOUSE_TERTIARY;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

bool GuestView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (event.is_keyboard()) {
    const fuchsia::ui::input::KeyboardEvent& key_event = event.keyboard();

    machina::InputEvent event;
    event.type = machina::InputEventType::KEYBOARD;
    event.key.hid_usage = key_event.hid_usage;
    switch (key_event.phase) {
      case fuchsia::ui::input::KeyboardEventPhase::PRESSED:
        event.key.state = machina::KeyState::PRESSED;
        break;
      case fuchsia::ui::input::KeyboardEventPhase::RELEASED:
      case fuchsia::ui::input::KeyboardEventPhase::CANCELLED:
        event.key.state = machina::KeyState::RELEASED;
        break;
      default:
        // Ignore events for unsupported phases.
        return true;
    }
    input_dispatcher_->Keyboard()->PostEvent(event, true);
    return true;
  } else if (event.is_pointer()) {
    const fuchsia::ui::input::PointerEvent& pointer_event = event.pointer();
    if (!view_ready_) {
      // Ignore pointer events that come in before the view is ready.
      return true;
    }
    machina::InputEvent event;
    switch (pointer_event.phase) {
      case fuchsia::ui::input::PointerEventPhase::MOVE:
        event.type = machina::InputEventType::POINTER;
        event.pointer.x = pointer_event.x * pointer_scale_x_;
        event.pointer.y = pointer_event.y * pointer_scale_y_;
        event.pointer.type = machina::PointerType::ABSOLUTE;
        break;
      case fuchsia::ui::input::PointerEventPhase::DOWN:
        event.type = machina::InputEventType::BUTTON;
        event.button.state = machina::KeyState::PRESSED;
        if (FromMozartButton(pointer_event.buttons, &event.button.button) !=
            ZX_OK) {
          // Ignore events for unsupported buttons.
          return true;
        }
        break;
      case fuchsia::ui::input::PointerEventPhase::UP:
        event.type = machina::InputEventType::BUTTON;
        event.button.state = machina::KeyState::RELEASED;
        if (FromMozartButton(pointer_event.buttons, &event.button.button) !=
            ZX_OK) {
          // Ignore events for unsupported buttons.
          return true;
        }
        break;
      default:
        // Ignore events for unsupported phases.
        return true;
    }
    // The pointer events get routed to the touch event queue because the
    // pointer positions are always absolute.
    input_dispatcher_->Touch()->PostEvent(event, true);
    return true;
  }
  return false;
}
