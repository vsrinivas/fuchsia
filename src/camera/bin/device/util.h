// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_UTIL_H_
#define SRC_CAMERA_BIN_DEVICE_UTIL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

// Safely unbinds a client connection, doing so on the connection's thread if it differs from the
// caller's thread.
template <class T>
inline void Unbind(fidl::InterfacePtr<T>& p) {
  if (!p) {
    return;
  }

  if (p.dispatcher() == async_get_default_dispatcher()) {
    p.Unbind();
    return;
  }

  async::PostTask(p.dispatcher(), [&]() { p.Unbind(); });
}

// Converts a camera2.hal.Config to a camera3.Configuration
inline fit::result<fuchsia::camera3::Configuration, zx_status_t> Convert(
    const fuchsia::camera2::hal::Config& config) {
  if (config.stream_configs.empty()) {
    FX_LOGS(ERROR) << "Config reported no streams.";
    return fit::error(ZX_ERR_INTERNAL);
  }
  fuchsia::camera3::Configuration ret{};
  for (const auto& stream_config : config.stream_configs) {
    if (stream_config.image_formats.empty()) {
      FX_LOGS(ERROR) << "Stream reported no image formats.";
      return fit::error(ZX_ERR_INTERNAL);
    }
    fuchsia::camera3::StreamProperties stream_properties{};
    stream_properties.frame_rate.numerator = stream_config.frame_rate.frames_per_sec_numerator;
    stream_properties.frame_rate.denominator = stream_config.frame_rate.frames_per_sec_denominator;
    stream_properties.image_format = stream_config.image_formats[0];
    // TODO(50908): Detect ROI support during initialization.
    stream_properties.supports_crop_region = true;
    ret.streams.push_back(std::move(stream_properties));
  }
  return fit::ok(std::move(ret));
}

// Waits until |deadline| for |event| to signal all bits in |signals_all| or any bits in
// |signals_any|, accumulating signaled bits into |pending|.
inline zx_status_t WaitMixed(const zx::event& event, zx_signals_t signals_all,
                             zx_signals_t signals_any, zx::time deadline, zx_signals_t* pending) {
  ZX_ASSERT(pending);
  *pending = ZX_SIGNAL_NONE;
  zx_status_t status = ZX_OK;
  while (status == ZX_OK) {
    zx_signals_t signaled{};
    status = event.wait_one(signals_all | signals_any, deadline, &signaled);
    *pending |= signaled;
    if ((*pending & signals_all) == signals_all) {
      return status;
    }
    if ((*pending & signals_any) != ZX_SIGNAL_NONE) {
      return status;
    }
  }
  return status;
}

// Represents the mute state of a device as defined by the Device.WatchMuteState API.
struct MuteState {
  bool software_muted = false;
  bool hardware_muted = false;
  // Returns true iff any mute is active.
  bool muted() const { return software_muted || hardware_muted; }
  bool operator==(const MuteState& other) const {
    return other.software_muted == software_muted && other.hardware_muted == hardware_muted;
  }
};

#endif  // SRC_CAMERA_BIN_DEVICE_UTIL_H_
