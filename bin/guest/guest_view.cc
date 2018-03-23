// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_view.h"

#include <semaphore.h>

#include "lib/fsl/tasks/message_loop.h"
#include <fuchsia/cpp/views_v1.h>
#include <fuchsia/cpp/views_v1.h>

// For now we expose a fixed size display to the guest. Scenic will scale this
// buffer to the actual window size on the host.
static constexpr uint32_t kDisplayWidth = 1024;
static constexpr uint32_t kDisplayHeight = 768;

// static
zx_status_t ScenicScanout::Create(
    component::ApplicationContext* application_context,
    machina::InputDispatcher* input_dispatcher,
    fbl::unique_ptr<GpuScanout>* out) {
  *out = fbl::make_unique<ScenicScanout>(application_context, input_dispatcher);
  return ZX_OK;
}

ScenicScanout::ScenicScanout(component::ApplicationContext* application_context,
                             machina::InputDispatcher* input_dispatcher)
    : input_dispatcher_(input_dispatcher),
      application_context_(application_context),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {
  // The actual framebuffer can't be created until we've connected to the
  // mozart service.
  SetReady(false);

  application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
      [this](f1dl::InterfaceRequest<mozart::ViewProvider> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void ScenicScanout::CreateView(
    f1dl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
    f1dl::InterfaceRequest<component::ServiceProvider> view_services) {
  if (view_) {
    FXL_LOG(ERROR) << "CreateView called when a view already exists";
    return;
  }
  auto view_manager =
      application_context_->ConnectToEnvironmentService<views_v1::ViewManager>();
  view_ = fbl::make_unique<GuestView>(this, input_dispatcher_,
                                      fbl::move(view_manager),
                                      fbl::move(view_owner_request));
  if (view_) {
    view_->SetReleaseHandler([this] { view_.reset(); });
  }
  SetReady(true);
}

void ScenicScanout::InvalidateRegion(const machina::GpuRect& rect) {
  task_runner_->PostTask([this] { view_->InvalidateScene(); });
}

GuestView::GuestView(
    machina::GpuScanout* scanout,
    machina::InputDispatcher* input_dispatcher,
    views_v1::ViewManagerPtr view_manager,
    f1dl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Guest"),
      background_node_(session()),
      material_(session()),
      input_dispatcher_(input_dispatcher),
      previous_pointer_x_(kDisplayWidth * .5f),
      previous_pointer_y_(kDisplayHeight * .5f) {
  background_node_.SetMaterial(material_);
  parent_node().AddChild(background_node_);

  image_info_.width = kDisplayWidth;
  image_info_.height = kDisplayHeight;
  image_info_.stride = kDisplayWidth * 4;
  image_info_.pixel_format = images::PixelFormat::BGRA_8;

  // Allocate a framebuffer and attach it as a GPU scanout.
  memory_ = fbl::make_unique<scenic_lib::HostMemory>(
      session(), scenic_lib::Image::ComputeSize(image_info_));
  machina::GpuBitmap bitmap(kDisplayWidth, kDisplayHeight,
                            ZX_PIXEL_FORMAT_ARGB_8888,
                            reinterpret_cast<uint8_t*>(memory_->data_ptr()));
  scanout->SetBitmap(std::move(bitmap));
}

GuestView::~GuestView() = default;

void GuestView::OnSceneInvalidated(images::PresentationInfoPtr presentation_info) {
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

  scenic_lib::HostImage image(*memory_, 0u, image_info_.Clone());
  material_.SetTexture(image);
}

zx_status_t FromMozartButton(uint32_t event, machina::Button* button) {
  switch (event) {
    case mozart::kMousePrimaryButton:
      *button = machina::Button::BTN_MOUSE_PRIMARY;
      return ZX_OK;
    case mozart::kMouseSecondaryButton:
      *button = machina::Button::BTN_MOUSE_SECONDARY;
      return ZX_OK;
    case mozart::kMouseTertiaryButton:
      *button = machina::Button::BTN_MOUSE_TERTIARY;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

bool GuestView::OnInputEvent(input::InputEventPtr event) {
  if (event->is_keyboard()) {
    const input::KeyboardEventPtr& key_event = event->keyboard();

    machina::InputEvent event;
    event.type = machina::InputEventType::KEYBOARD;
    event.key.hid_usage = key_event->hid_usage;
    switch (key_event->phase) {
      case input::KeyboardEventPhase::PRESSED:
        event.key.state = machina::KeyState::PRESSED;
        break;
      case input::KeyboardEventPhase::RELEASED:
      case input::KeyboardEventPhase::CANCELLED:
        event.key.state = machina::KeyState::RELEASED;
        break;
      default:
        // Ignore events for unsupported phases.
        return true;
    }
    input_dispatcher_->Keyboard()->PostEvent(event, true);
    return true;
  } else if (event->is_pointer()) {
    const input::PointerEventPtr& pointer_event = event->pointer();

    machina::InputEvent event;
    switch (pointer_event->phase) {
      case input::PointerEventPhase::MOVE:
        event.type = machina::InputEventType::POINTER;
        // TODO(PD-102): Convert this to use absolute pointer events.
        event.pointer.x = pointer_event->x - previous_pointer_x_;
        event.pointer.y = pointer_event->y - previous_pointer_y_;
        event.pointer.type = machina::PointerType::RELATIVE;
        previous_pointer_x_ = pointer_event->x;
        previous_pointer_y_ = pointer_event->y;
        break;
      case input::PointerEventPhase::DOWN:
        event.type = machina::InputEventType::BUTTON;
        event.button.state = machina::KeyState::PRESSED;
        if (FromMozartButton(pointer_event->buttons, &event.button.button) !=
            ZX_OK) {
          // Ignore events for unsupported buttons.
          return true;
        }
        break;
      case input::PointerEventPhase::UP:
        event.type = machina::InputEventType::BUTTON;
        event.button.state = machina::KeyState::RELEASED;
        if (FromMozartButton(pointer_event->buttons, &event.button.button) !=
            ZX_OK) {
          // Ignore events for unsupported buttons.
          return true;
        }
        break;
      default:
        // Ignore events for unsupported phases.
        return true;
    }
    input_dispatcher_->Pointer()->PostEvent(event, true);
    return true;
  }
  return false;
}
