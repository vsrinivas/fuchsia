// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/power.h"

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>
#include <zircon/pixelformat.h>

#include <memory>
#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace i915 {

class PowerTest : public ::testing::Test {
 public:
  PowerTest() = default;

  void SetUp() override {
    regs_.resize(kMinimumRegCount);
    reg_region_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.data(), sizeof(uint32_t),
                                                                kMinimumRegCount);
    mmio_buffer_.emplace(reg_region_->GetMmioBuffer());
  }

  void TearDown() override {}

 protected:
  constexpr static uint32_t kMinimumRegCount = 0x50000 / sizeof(uint32_t);
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> reg_region_;
  std::vector<ddk_fake::FakeMmioReg> regs_;
  std::optional<fdf::MmioBuffer> mmio_buffer_;
};

// Verify setting Power Well 2 status on Skylake platform.
TEST_F(PowerTest, Skl_PowerWell2) {
  auto kPwrWellCtlIndex = registers::PowerWellControl2::Get().addr() / sizeof(uint32_t);
  auto kFuseStatusIndex = registers::FuseStatus::Get().addr() / sizeof(uint32_t);

  bool pg2_status = false;

  regs_[kPwrWellCtlIndex].SetWriteCallback(
      [&pg2_status](uint64_t in) { pg2_status = in & (1 << 31); });

  regs_[kPwrWellCtlIndex].SetReadCallback([&pg2_status]() -> uint64_t { return pg2_status << 30; });

  regs_[kFuseStatusIndex].SetReadCallback([&pg2_status]() -> uint64_t { return pg2_status << 25; });

  constexpr uint16_t kSklDeviceId = 0x191b;
  auto skl_power = Power::New(&*mmio_buffer_, kSklDeviceId);

  {
    auto power_well2_ref = PowerWellRef(skl_power.get(), PowerWellId::PG2);
    EXPECT_TRUE(pg2_status);
  }
  EXPECT_FALSE(pg2_status);

  // Test resuming power well state.
  {
    auto power_well2_ref = PowerWellRef(skl_power.get(), PowerWellId::PG2);
    EXPECT_TRUE(pg2_status);

    pg2_status = false;

    skl_power->Resume();
    EXPECT_TRUE(pg2_status);
  }
  EXPECT_FALSE(pg2_status);

  // Test creating and deleting multiple power well refs to PG2.
  // PG2 should be disabled only after all power well refs are removed.
  constexpr size_t kNumRefs = 10;
  std::vector<PowerWellRef> refs(kNumRefs);
  for (size_t i = 0; i < kNumRefs; i++) {
    refs[i] = PowerWellRef(skl_power.get(), PowerWellId::PG2);
  }

  for (size_t i = 0; i < kNumRefs; i++) {
    EXPECT_TRUE(pg2_status);
    refs[i] = {};
  }
  EXPECT_FALSE(pg2_status);
}

}  // namespace i915
