// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"
#include "src/lib/fxl/logging.h"

#include <fcntl.h>
#include <fuchsia/hardware/backlight/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>

namespace display {

#define DEVICE_PATH "/dev/class/backlight/000"
#define BRIGHTNESS_BASE 255;

Display::Display(zx::channel channel) : channel_(std::move(channel)) {}

Display::~Display() {}

Display* Display::GetDisplay() {
  const int fd = open(DEVICE_PATH, O_RDWR);

  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open backlight";
    return NULL;
  }

  zx::channel channel;
  if (fdio_get_service_handle(fd, channel.reset_and_get_address()) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get backlight channel";
    return NULL;
  }

  return new Display(std::move(channel));
}

bool Display::GetBrightness(double* brightness) {
  fuchsia_hardware_backlight_State state;
  zx_status_t status =
      fuchsia_hardware_backlight_DeviceGetState(channel_.get(), &state);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Getting backlight state failed";
    return false;
  }

  *brightness = (state.brightness * 1.0f) / BRIGHTNESS_BASE;
  return true;
}

bool Display::SetBrightness(double brightness) {
  const uint32_t adjustBrightness = brightness * BRIGHTNESS_BASE;
  fuchsia_hardware_backlight_State state = {
      .on = brightness > 0, .brightness = (uint8_t)adjustBrightness};
  zx_status_t status =
      fuchsia_hardware_backlight_DeviceSetState(channel_.get(), &state);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Setting backlight state failed";
    return false;
  }

  return true;
}

}  // namespace display
