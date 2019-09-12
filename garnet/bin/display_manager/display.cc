// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "display.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>

#include "src/lib/fxl/logging.h"

namespace display {

#define DEVICE_PATH "/dev/class/backlight/000"

Display::Display(zx::channel channel)
    : client_(FidlBacklight::Device::SyncClient(std::move(channel))) {}

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
  auto response = client_.GetStateNormalized();
  zx_status_t status = response.ok() ? (response->result.is_err() ? response->result.err() : ZX_OK)
                                     : response.status();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Getting backlight state failed with " << status;
    return false;
  }
  *brightness = response->result.response().state.brightness;
  return true;
}

bool Display::SetBrightness(double brightness) {
  FidlBacklight::State state = {.backlight_on = brightness > 0, .brightness = brightness};
  auto response = client_.SetStateNormalized(state);
  zx_status_t status = response.ok() ? (response->result.is_err() ? response->result.err() : ZX_OK)
                                     : response.status();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Setting backlight state failed with " << status;
    return false;
  }
  return true;
}

}  // namespace display
