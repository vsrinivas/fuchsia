// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_LOGGING_H_
#define SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_LOGGING_H_

#include <fidl/fuchsia.audio.device/cpp/common_types.h>

#include <optional>
#include <ostream>

namespace media_audio {

inline constexpr bool kLogDeviceDetection = false;

inline std::ostream& operator<<(
    std::ostream& out, const std::optional<fuchsia_audio_device::DeviceType>& device_type) {
  if (device_type) {
    switch (*device_type) {
      case fuchsia_audio_device::DeviceType::kInput:
        return (out << " INPUT");
      case fuchsia_audio_device::DeviceType::kOutput:
        return (out << "OUTPUT");
      default:
        return (out << "UNKNOWN");
    }
  }
  return (out << "NONE (non-compliant)");
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_LOGGING_H_
