// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_CACHE_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_CACHE_H_

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <map>
#include <memory>
#include <optional>

/// Maintains a cache of (key, value) pair and deletes the cache on frequent intervals. This
/// uses a variant of LRU algorithm to delete cache entries.
/// This class is not thread safe.
template <typename Key, typename Value>
class Cache {
 public:
  explicit Cache(zx::duration cleanup_interval, async_dispatcher* dispatcher)
      : cleanup_interval_(cleanup_interval), dispatcher_(dispatcher) {}

  /// Gets value associated with given key if present in the cache.
  std::optional<Value> GetValue(const Key& key) {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return std::nullopt;
    }
    it->second->last_accessed_ = get_current_time(dispatcher_);
    return std::optional(it->second->value_);
  }

  /// Tries to add key, value pair to the cache.
  /// If the key already has associated value, old value is dropped.
  void Add(Key key, Value value) {
    auto schedule_cleanup = map_.empty();  // only schedule again if the cache was empty.
    auto v = std::make_unique<Internal>(std::move(value), get_current_time(dispatcher_));
    map_[key] = std::move(v);
    if (schedule_cleanup) {
      ScheduleCleanup();
    }
  }

 private:
  struct Internal {
    Value value_;
    zx::time last_accessed_;

    explicit Internal(Value value, zx::time last_accessed)
        : value_(std::move(value)), last_accessed_(last_accessed) {}
  };

  void RunCleanup() {
    for (auto it = map_.begin(); it != map_.end();) {
      if (it->second->last_accessed_ + cleanup_interval_ < get_current_time(dispatcher_)) {
        it = map_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void ScheduleCleanup() {
    auto status = async::PostDelayedTask(
        dispatcher_,
        [this]() {
          RunCleanup();
          if (!map_.empty()) {
            ScheduleCleanup();
          }
        },
        cleanup_interval_);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Cannot schedule cache cleanup: " << zx_status_get_string(status);
    }
  }

  zx::time get_current_time(async_dispatcher* dispatcher) {
    return zx::time(async_now(dispatcher));
  }

  std::map<Key, std::unique_ptr<Internal>> map_;
  zx::duration cleanup_interval_;
  async_dispatcher* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_CACHE_H_
