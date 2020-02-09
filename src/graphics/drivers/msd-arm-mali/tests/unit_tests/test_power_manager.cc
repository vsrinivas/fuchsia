// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "fbl/algorithm.h"
#include "gtest/gtest.h"
#include "mock/mock_mmio.h"
#include "platform_buffer.h"
#include "power_manager.h"
#include "registers.h"

class TestPowerManager {
 public:
  void MockEnable() {
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    auto power_manager = std::make_unique<PowerManager>(reg_io.get());

    constexpr uint32_t kShaderOnOffset =
        static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
        static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);
    constexpr uint32_t kShaderOnHighOffset = kShaderOnOffset + 4;
    constexpr uint32_t kDummyHighValue = 1500;
    reg_io->Write32(kShaderOnHighOffset, kDummyHighValue);
    power_manager->EnableCores(reg_io.get(), 0xf);
    // Higher word shouldn't be written to because none of them are being
    // enabled.
    EXPECT_EQ(kDummyHighValue, reg_io->Read32(kShaderOnHighOffset));

    registers::CoreReadyState::CoreType actions[] = {registers::CoreReadyState::CoreType::kShader,
                                                     registers::CoreReadyState::CoreType::kL2,
                                                     registers::CoreReadyState::CoreType::kTiler};
    for (size_t i = 0; i < fbl::count_of(actions); i++) {
      uint32_t offset =
          static_cast<uint32_t>(actions[i]) +
          static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);

      if (actions[i] == registers::CoreReadyState::CoreType::kShader)
        EXPECT_EQ(0xfu, reg_io->Read32(offset));
      else
        EXPECT_EQ(1u, reg_io->Read32(offset));
    }
  }

  void MockDisable() {
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    auto power_manager = std::make_unique<PowerManager>(reg_io.get());

    constexpr uint64_t kCoresEnabled = 2;
    constexpr uint32_t kShaderReadyOffset =
        static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
        static_cast<uint32_t>(registers::CoreReadyState::StatusType::kReady);
    reg_io->Write32(kShaderReadyOffset, kCoresEnabled);

    power_manager->DisableShaders(reg_io.get());

    registers::CoreReadyState::CoreType actions[] = {registers::CoreReadyState::CoreType::kShader,
                                                     registers::CoreReadyState::CoreType::kL2,
                                                     registers::CoreReadyState::CoreType::kTiler};
    for (size_t i = 0; i < fbl::count_of(actions); i++) {
      uint32_t offset =
          static_cast<uint32_t>(actions[i]) +
          static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOff);

      if (actions[i] == registers::CoreReadyState::CoreType::kShader)
        EXPECT_EQ(kCoresEnabled, reg_io->Read32(offset));
      else
        EXPECT_EQ(0u, reg_io->Read32(offset));
    }
    power_manager->DisableL2(reg_io.get());
    for (size_t i = 0; i < fbl::count_of(actions); i++) {
      uint32_t offset =
          static_cast<uint32_t>(actions[i]) +
          static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOff);

      if (actions[i] == registers::CoreReadyState::CoreType::kShader)
        EXPECT_EQ(kCoresEnabled, reg_io->Read32(offset));
      else
        EXPECT_EQ(1u, reg_io->Read32(offset));
    }
  }

  void TimeCoalesce() {
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    PowerManager power_manager(reg_io.get());

    for (int i = 0; i < 100; i++) {
      power_manager.UpdateGpuActive(true);
      usleep(5000);
      power_manager.UpdateGpuActive(false);
      usleep(5000);
    }

    auto time_periods = power_manager.time_periods();
    // There can be 4 time periods containing the last 100ms - for example 45
    // ms (oldest), 45 ms, 45 ms, 5 ms (most recent). More than that and either
    // one ends more than 100ms ago or one could be combined with the one
    // previous to make a chunk that's < 50 ms.
    EXPECT_GE(4u, time_periods.size());
  }
};

TEST(PowerManager, MockEnable) {
  TestPowerManager test;
  test.MockEnable();
}

TEST(PowerManager, MockDisable) {
  TestPowerManager test;
  test.MockDisable();
}

TEST(PowerManager, TimeAccumulation) {
  auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
  PowerManager power_manager(reg_io.get());
  power_manager.UpdateGpuActive(true);
  usleep(150 * 1000);

  std::chrono::steady_clock::duration total_time;
  std::chrono::steady_clock::duration active_time;
  power_manager.GetGpuActiveInfo(&total_time, &active_time);
  EXPECT_LE(100u, std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count());
  EXPECT_EQ(total_time, active_time);

  usleep(150 * 1000);

  uint64_t before_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
  uint32_t time_buffer;
  EXPECT_TRUE(power_manager.GetTotalTime(&time_buffer));
  magma_total_time_query_result result;
  uint64_t after_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();

  auto buffer = magma::PlatformBuffer::Import(time_buffer);
  EXPECT_TRUE(buffer);
  EXPECT_TRUE(buffer->Read(&result, 0, sizeof(result)));

  EXPECT_LE(before_time_ns, result.monotonic_time_ns);
  EXPECT_LE(result.monotonic_time_ns, after_time_ns);

  // GetGpuActiveInfo should throw away old information, but the GetTotalTime count should be able
  // to go higher. We slept a total of 300ms above, so the time should be well over 250ms.
  constexpr uint32_t k250MsInNs = 250'000'000;
  EXPECT_LE(k250MsInNs, result.gpu_time_ns);
}

TEST(PowerManager, TimeCoalesce) {
  TestPowerManager test;
  test.TimeCoalesce();
}
