// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fs/watchdog/watchdog.h>

namespace fs_watchdog {
namespace {

class Watchdog : public WatchdogInterface {
  zx::status<> Start() override { return zx::ok(); }

  zx::status<> ShutDown() override { return zx::ok(); }
  zx::status<> Track(OperationTracker* tracker) override { return zx::ok(); }
  zx::status<> Untrack(OperationTrackerId id) override { return zx::ok(); }
  ~Watchdog() override { ZX_DEBUG_ASSERT(ShutDown().is_ok()); }
};

}  // namespace
std::unique_ptr<WatchdogInterface> CreateWatchdog(const Options& options) {
  auto watchdog = new Watchdog();
  std::unique_ptr<WatchdogInterface> ret(watchdog);
  return ret;
}

}  // namespace fs_watchdog
