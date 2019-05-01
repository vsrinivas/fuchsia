// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKLATENCY_FB_VIEW_H_
#define GARNET_LIB_VULKAN_TESTS_VKLATENCY_FB_VIEW_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ui/input/device_state.h>
#include <lib/ui/input/input_device_impl.h>

#include <memory>
#include <unordered_map>

#include "garnet/bin/ui/input_reader/input_reader.h"
#include "garnet/lib/vulkan/tests/vklatency/skia_gpu_painter.h"
#include "garnet/lib/vulkan/tests/vklatency/swapchain.h"
#include "src/lib/fxl/macros.h"

namespace examples {

class FbView : public fuchsia::ui::input::InputDeviceRegistry,
               public ui_input::InputDeviceImpl::Listener {
 public:
  FbView(async::Loop* loop, bool protected_output);
  ~FbView() = default;

 private:
  // |fuchsia::ui::input::InputDeviceRegistry|
  void RegisterDevice(fuchsia::ui::input::DeviceDescriptor descriptor,
                      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
                          input_device) override;

  // |ui_input::InputDeviceImpl::Listener|
  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) override;
  void OnReport(ui_input::InputDeviceImpl* input_device,
                fuchsia::ui::input::InputReport report) override;

  void OnInputEvent(fuchsia::ui::input::InputEvent event);

  ui_input::InputReader input_reader_;
  typedef struct {
    std::unique_ptr<ui_input::InputDeviceImpl> device_impl;
    std::unique_ptr<ui_input::DeviceState> device_state;
  } InputDeviceTracker;
  std::unordered_map<uint32_t /* device_id */, InputDeviceTracker>
      input_devices_;

  Swapchain vk_swapchain_;
  std::unique_ptr<SkiaGpuPainter> painter_;

  zx::duration draw_interval_;
  async::Loop* const message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FbView);
};

}  // namespace examples

#endif  // GARNET_LIB_VULKAN_TESTS_VKLATENCY_FB_VIEW_H_
