// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <string>

namespace monitor {

namespace test {
class PressureUnitTest;
}

enum Level : size_t {
  kCritical = 0,
  kWarning,
  kNormal,
  kNumLevels,
};
static constexpr size_t kNameLength = 10;
static constexpr std::array<char[kNameLength], Level::kNumLevels + 1> kLevelNames = {
    "CRITICAL", "WARNING", "NORMAL", "UNSET"};

class Pressure {
 public:
  explicit Pressure(bool watch_for_changes);

 private:
  zx_status_t InitMemPressureEvents();
  void WatchForChanges();
  void WaitOnLevelChange();
  void OnLevelChanged(zx_handle_t handle);

  Level level_ = Level::kNumLevels;
  std::array<zx::event, Level::kNumLevels> events_;
  std::array<zx_wait_item_t, Level::kNumLevels> wait_items_;
  async::TaskClosureMethod<Pressure, &Pressure::WatchForChanges> task_{this};
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  friend class test::PressureUnitTest;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_
