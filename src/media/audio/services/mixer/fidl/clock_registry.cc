// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {

zx::status<zx_koid_t> ZxClockToKoid(const zx::clock& handle) {
  zx_info_handle_basic_t info;
  auto status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(info.koid);
}

}  // namespace

void ClockRegistry::Add(std::shared_ptr<Clock> clock) {
  // Garbage collect to avoid unbounded growth.
  for (auto it = clocks_.begin(); it != clocks_.end();) {
    if (it->second.expired()) {
      it = clocks_.erase(it);
    } else {
      it++;
    }
  }

  const auto koid = clock->koid();
  const auto [it, is_new] = clocks_.emplace(koid, std::move(clock));
  FX_CHECK(is_new) << "cannot duplicate clocks";
}

zx::status<std::shared_ptr<Clock>> ClockRegistry::Find(zx_koid_t koid) {
  auto it = clocks_.find(koid);
  if (it == clocks_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto clock = it->second.lock();
  if (!clock) {
    clocks_.erase(it);
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return zx::ok(std::move(clock));
}

zx::status<std::shared_ptr<Clock>> ClockRegistry::Find(const zx::clock& handle) {
  auto koid_result = ZxClockToKoid(handle);
  if (!koid_result.is_ok()) {
    return koid_result.take_error();
  }

  auto koid = koid_result.value();
  return Find(koid);
}

}  // namespace media_audio
