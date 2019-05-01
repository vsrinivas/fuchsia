// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/vulkan/tests/vklatency/fb_view.h"

#include <lib/async/cpp/task.h>

#include "src/lib/fxl/logging.h"

namespace examples {

FbView::FbView(async::Loop* loop, bool protected_output)
    : input_reader_(this),
      vk_swapchain_(protected_output),
      draw_interval_(zx::msec(3)),
      message_loop_(loop) {
  input_reader_.Start();
  const bool rv = vk_swapchain_.Initialize(zx::channel(), std::nullopt);
  FXL_CHECK(rv);
  painter_ = std::make_unique<SkiaGpuPainter>(&vk_swapchain_);
}

void FbView::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device) {
  if (!descriptor.touchscreen && !descriptor.mouse)
    return;
  static uint32_t device_id = 0;
  auto device_impl = std::make_unique<ui_input::InputDeviceImpl>(
      ++device_id, std::move(descriptor), std::move(input_device), this);
  auto device_state = std::make_unique<ui_input::DeviceState>(
      device_impl->id(), device_impl->descriptor(),
      [this](fuchsia::ui::input::InputEvent event) {
        OnInputEvent(std::move(event));
      });
  input_devices_.emplace(
      device_id,
      InputDeviceTracker{std::move(device_impl), std::move(device_state)});
}

void FbView::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  const uint32_t device_id = input_device->id();
  if (input_devices_.count(device_id) == 0)
    return;
  input_devices_.erase(device_id);
  FXL_LOG(ERROR) << "Input device disconnected";
}

void FbView::OnReport(ui_input::InputDeviceImpl* input_device,
                      fuchsia::ui::input::InputReport report) {
  const uint32_t device_id = input_device->id();
  if (input_devices_.count(device_id) == 0)
    return;

  auto image_size = vk_swapchain_.GetImageSize();
  fuchsia::math::Size size;
  size.width = image_size.width;
  size.height = image_size.height;
  input_devices_[device_id].device_state->Update(std::move(report), size);
}

void FbView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  const bool had_pending_draw = painter_->HasPendingDraw();
  painter_->OnInputEvent(std::move(event));

  // We don't know about vsync interval without using Scenic or display
  // directly. In order to limit number of draw calls queued, accumulate them in
  // an interval and call DrawImage() for the first input that results in a
  // pending draw.
  if (!had_pending_draw && painter_->HasPendingDraw()) {
    async::PostDelayedTask(
        message_loop_->dispatcher(), [this]() { painter_->DrawImage(); },
        draw_interval_);
  }
}

}  // namespace examples
