// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "device_id.h"
#include "forcewake.h"
#include "mock/mock_mmio.h"
#include "msd_intel_device.h"
#include "platform_mmio.h"
#include "registers.h"

class TestForceWake : public testing::TestWithParam<registers::ForceWake::Domain> {
 public:
  TestForceWake() : domain_(GetParam()) {
    switch (domain_) {
      case registers::ForceWake::RENDER:
        offset_ = registers::ForceWake::kRenderOffset;
        status_offset_ = registers::ForceWake::kRenderStatusOffset;
        break;
      case registers::ForceWake::GEN9_MEDIA:
        offset_ = registers::ForceWake::kGen9MediaOffset;
        status_offset_ = registers::ForceWake::kGen9MediaStatusOffset;
        break;
      case registers::ForceWake::GEN12_VDBOX0:
        offset_ = registers::ForceWake::kGen12Vdbox0Offset;
        status_offset_ = registers::ForceWake::kGen12Vdbox0StatusOffset;
        break;
    }

    register_io_ = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(2 * 1024 * 1024));
  }

  std::unique_ptr<MsdIntelRegisterIo> register_io_;
  registers::ForceWake::Domain domain_;
  uint32_t offset_;
  uint32_t status_offset_;
};

TEST_P(TestForceWake, Reset) {
  register_io_->mmio()->Write32(0, offset_);

  ForceWake::reset(register_io_.get(), domain_);

  EXPECT_EQ(0xFFFF0000, register_io_->mmio()->Read32(offset_));
}

TEST_P(TestForceWake, Request) {
  register_io_->mmio()->Write32(0, status_offset_);

  // Verify timeout waiting for status
  auto start = std::chrono::high_resolution_clock::now();
  ForceWake::request(register_io_.get(), domain_);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;

  EXPECT_EQ(0x00010001u, register_io_->mmio()->Read32(offset_));
  EXPECT_GE(elapsed.count(), ForceWake::kRetryMaxMs);
}

TEST_P(TestForceWake, Release) {
  register_io_->mmio()->Write32(0xFFFFFFFF, status_offset_);

  // Verify timeout waiting for status
  auto start = std::chrono::high_resolution_clock::now();
  ForceWake::release(register_io_.get(), domain_);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;

  EXPECT_EQ(0x00010000u, register_io_->mmio()->Read32(offset_));
  EXPECT_GE(elapsed.count(), ForceWake::kRetryMaxMs);
}

INSTANTIATE_TEST_SUITE_P(TestForceWake, TestForceWake,
                         testing::Values(registers::ForceWake::RENDER,
                                         registers::ForceWake::GEN9_MEDIA,
                                         registers::ForceWake::GEN12_VDBOX0),
                         [](testing::TestParamInfo<registers::ForceWake::Domain> info) {
                           switch (info.param) {
                             case registers::ForceWake::RENDER:
                               return "RENDER";
                             case registers::ForceWake::GEN9_MEDIA:
                               return "GEN9_MEDIA";
                             case registers::ForceWake::GEN12_VDBOX0:
                               return "GEN12_VDBOX0";
                             default:
                               return "Unknown";
                           }
                         });
