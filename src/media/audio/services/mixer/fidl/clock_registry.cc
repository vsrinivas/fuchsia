// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

#include <lib/syslog/cpp/macros.h>

namespace media_audio {

zx::status<zx_koid_t> ClockRegistry::ZxClockToKoid(const zx::clock& clock) {
  zx_info_handle_basic_t info;
  auto status = clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(info.koid);
}

}  // namespace media_audio
