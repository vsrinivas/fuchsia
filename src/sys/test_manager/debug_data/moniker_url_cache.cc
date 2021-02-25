// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "moniker_url_cache.h"

#include <lib/async/cpp/task.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <memory>

static zx::time get_current_time(async_dispatcher* dispatcher) {
  return zx::time(async_now(dispatcher));
}

MonikerUrlCache::MonikerUrlCache(zx::duration cleanup_interval, async_dispatcher* dispatcher)
    : cleanup_interval_(cleanup_interval), dispatcher_(dispatcher) {}

void MonikerUrlCache::ScheduleCleanup() {
  auto status = async::PostDelayedTask(
      dispatcher_,
      [this]() {
        RunCleanup();
        if (!cache_.empty()) {
          ScheduleCleanup();
        }
      },
      cleanup_interval_);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Cannot schedule cache cleanup: " << zx_status_get_string(status);
  }
}

void MonikerUrlCache::RunCleanup() {
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (it->second->last_accessed_ + cleanup_interval_ < get_current_time(dispatcher_)) {
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}

bool MonikerUrlCache::Add(std::string moniker, std::string test_url) {
  auto schedule_cleanup = cache_.empty();  // only schedule again if the cache was empty.
  auto value =
      std::make_unique<ComponentUrlValue>(std::move(test_url), get_current_time(dispatcher_));
  auto ret = cache_.emplace(std::move(moniker), std::move(value));
  if (schedule_cleanup) {
    ScheduleCleanup();
  }
  return ret.second;
}

std::optional<std::string> MonikerUrlCache::GetTestUrl(const std::string& moniker) {
  auto it = cache_.find(moniker);
  if (it == cache_.end()) {
    return std::nullopt;
  }
  it->second->last_accessed_ = get_current_time(dispatcher_);
  return std::optional(it->second->test_url_);
}

MonikerUrlCache::ComponentUrlValue::ComponentUrlValue(std::string test_url, zx::time last_accessed)
    : test_url_(std::move(test_url)), last_accessed_(last_accessed) {}
