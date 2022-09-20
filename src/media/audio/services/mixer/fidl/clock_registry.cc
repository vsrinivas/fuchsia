// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/clock_registry.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {

namespace {
zx::status<zx_info_handle_basic_t> ZxClockInfo(const zx::clock& handle) {
  zx_info_handle_basic_t info;
  auto status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(info);
}

zx::status<zx_koid_t> ZxClockToKoid(const zx::clock& handle) {
  auto result = ZxClockInfo(handle);
  if (!result.is_ok()) {
    return result.take_error();
  }
  return zx::ok(result.value().koid);
}
}  // namespace

std::shared_ptr<const Clock> ClockRegistry::SystemMonotonicClock() const {
  return factory_->SystemMonotonicClock();
}

zx::status<std::pair<std::shared_ptr<Clock>, zx::clock>>
ClockRegistry::CreateGraphControlledClock() {
  auto name = std::string("GraphControlledClock") + std::to_string(num_graph_controlled_);
  num_graph_controlled_++;

  auto create_result = factory_->CreateGraphControlledClock(name);
  if (!create_result.is_ok()) {
    return create_result;
  }

  // Check that the handle has the correct ID and rights.
  const auto info_result = ZxClockInfo(create_result.value().second);
  FX_CHECK(info_result.is_ok()) << "Cannot get info of graph-controlled clock: "
                                << info_result.status_string();
  const auto rights = info_result.value().rights;
  FX_CHECK((rights & ZX_RIGHT_DUPLICATE) != 0 && (rights & ZX_RIGHT_TRANSFER) != 0 &&
           (rights & ZX_RIGHT_WRITE) == 0)
      << "Graph-controlled clock has invalid rights: 0x" << std::hex << rights;

  // Add the clock.
  if (auto add_result = AddClock(create_result.value().first); !add_result.is_ok()) {
    return add_result.take_error();
  }
  return create_result;
}

zx::status<std::shared_ptr<Clock>> ClockRegistry::CreateUserControlledClock(zx::clock handle,
                                                                            std::string_view name,
                                                                            uint32_t domain) {
  auto clock_result =
      factory_->CreateWrappedClock(std::move(handle), name, domain, /*adjustable=*/false);
  if (!clock_result.is_ok()) {
    return clock_result.take_error();
  }

  auto& clock = clock_result.value();
  if (auto add_result = AddClock(clock); !add_result.is_ok()) {
    return add_result.take_error();
  }
  return zx::ok(clock);
}

zx::status<> ClockRegistry::AddClock(std::shared_ptr<Clock> clock) {
  const auto koid = clock->koid();
  const auto [it, new_element] = clocks_.emplace(koid, std::move(clock));
  if (!new_element) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  return zx::ok();
}

zx::status<std::shared_ptr<Clock>> ClockRegistry::FindClock(const zx::clock& handle) {
  auto koid_result = ZxClockToKoid(handle);
  if (!koid_result.is_ok()) {
    return koid_result.take_error();
  }

  const auto koid = koid_result.value();
  auto it = clocks_.find(koid);
  if (it == clocks_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return zx::ok(it->second);
}

zx::status<> ClockRegistry::ForgetClock(const zx::clock& handle) {
  auto koid_result = ZxClockToKoid(handle);
  if (!koid_result.is_ok()) {
    return koid_result.take_error();
  }

  const auto koid = koid_result.value();
  auto it = clocks_.find(koid);
  if (it == clocks_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  clocks_.erase(it);
  return zx::ok();
}

std::shared_ptr<Timer> ClockRegistry::CreateTimer() { return factory_->CreateTimer(); }

}  // namespace media_audio
