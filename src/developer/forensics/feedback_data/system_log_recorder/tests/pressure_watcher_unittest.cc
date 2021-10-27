// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/memorypressure/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"
#include "src/developer/forensics/testing/stubs/memory_pressure.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics::feedback_data::system_log_recorder {
namespace {

class PressureWatcherTest : public UnitTestFixture {
 public:
  PressureWatcherTest()
      : pressure_provider_(std::make_unique<stubs::MemoryPressure>(dispatcher())) {
    InjectServiceProvider(pressure_provider_.get());
  }

 protected:
  void ChangePressureLevel(const fuchsia::memorypressure::Level level) {
    pressure_provider_->ChangePressureLevel(level);
    RunLoopUntilIdle();
  }

 private:
  std::unique_ptr<stubs::MemoryPressure> pressure_provider_;
};

TEST_F(PressureWatcherTest, OnLevelChangedFn_Called) {
  std::optional<fuchsia::memorypressure::Level> level{std::nullopt};
  PressureWatcher watcher(
      dispatcher(), services(),
      [&level](const fuchsia::memorypressure::Level out_level) { level = out_level; });
  RunLoopUntilIdle();

  EXPECT_EQ(level, std::nullopt);

  ChangePressureLevel(fuchsia::memorypressure::Level::NORMAL);
  EXPECT_EQ(level, fuchsia::memorypressure::Level::NORMAL);
  level = std::nullopt;

  ChangePressureLevel(fuchsia::memorypressure::Level::WARNING);
  EXPECT_EQ(level, fuchsia::memorypressure::Level::WARNING);
  level = std::nullopt;

  ChangePressureLevel(fuchsia::memorypressure::Level::CRITICAL);
  EXPECT_EQ(level, fuchsia::memorypressure::Level::CRITICAL);
  level = std::nullopt;
}

}  // namespace
}  // namespace forensics::feedback_data::system_log_recorder
