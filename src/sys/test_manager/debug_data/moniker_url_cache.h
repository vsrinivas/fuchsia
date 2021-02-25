// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_MONIKER_URL_CACHE_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_MONIKER_URL_CACHE_H_

#include <zircon/time.h>

#include <map>
#include <memory>

#include <src/lib/fxl/memory/weak_ptr.h>

#include "fuchsia/debugdata/cpp/fidl.h"
#include "lib/fit/optional.h"

/// Maintains a cache of (moniker, tets url) pair and deletes the cache on frequent intervals. This
/// uses a variant of LRU algorithm to delete cache entries.
/// This optimizes the case where a component makes multiple connections to debug data.
class MonikerUrlCache {
 public:
  explicit MonikerUrlCache(zx::duration cleanup_interval, async_dispatcher* dispatcher);

  /// Gets tets url associated with given moniker if present in the cache.
  std::optional<std::string> GetTestUrl(const std::string& moniker);

  /// Tries to add moniker, url pair to the cache.
  /// Returns false if moniker is already in the cache
  /// and does not insert new test url.
  bool Add(std::string moniker, std::string test_url);

 private:
  struct ComponentUrlValue {
    std::string test_url_;
    zx::time last_accessed_;

    explicit ComponentUrlValue(std::string test_url, zx::time last_accessed);
  };

  void RunCleanup();

  void ScheduleCleanup();

  std::map<std::string, std::unique_ptr<ComponentUrlValue>> cache_;
  zx::duration cleanup_interval_;
  async_dispatcher* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_MONIKER_URL_CACHE_H_
