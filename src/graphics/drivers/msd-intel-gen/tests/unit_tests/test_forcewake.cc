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

namespace {
struct TestParam {
  ForceWakeDomain domain;
  uint32_t device_id;
};
}  // namespace

class TestForceWake : public testing::TestWithParam<TestParam> {
 public:
  void SetUp() override {
    domain_ = GetParam().domain;
    switch (domain_) {
      case ForceWakeDomain::RENDER:
        offset_ = registers::ForceWakeRequest::kRenderOffset;
        status_offset_ = registers::ForceWakeStatus::kRenderStatusOffset;
        break;
      case ForceWakeDomain::GEN9_MEDIA:
        offset_ = registers::ForceWakeRequest::kGen9MediaOffset;
        status_offset_ = registers::ForceWakeStatus::kGen9MediaStatusOffset;
        break;
      case ForceWakeDomain::GEN12_VDBOX0:
        offset_ = registers::ForceWakeRequest::kGen12Vdbox0Offset;
        status_offset_ = registers::ForceWakeStatus::kGen12Vdbox0StatusOffset;
        break;
    }

    register_io_ = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(2 * 1024 * 1024));
    forcewake_ = std::make_unique<ForceWake>(register_io_.get(), GetParam().device_id);
  }

  std::unique_ptr<MsdIntelRegisterIo> register_io_;
  ForceWakeDomain domain_;
  std::unique_ptr<ForceWake> forcewake_;
  uint32_t offset_;
  uint32_t status_offset_;
};

TEST_P(TestForceWake, Reset) {
  register_io_->mmio()->Write32(0, offset_);

  ASSERT_TRUE(forcewake_->Reset(register_io_.get(), domain_));

  EXPECT_EQ(0xFFFF0000, register_io_->mmio()->Read32(offset_));
}

TEST_P(TestForceWake, Request) {
  register_io_->mmio()->Write32(0, status_offset_);

  // Verify timeout waiting for status
  auto start = std::chrono::high_resolution_clock::now();
  ASSERT_FALSE(forcewake_->Request(register_io_.get(), domain_));

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> elapsed = end - start;

  EXPECT_EQ(0x00010001u, register_io_->mmio()->Read32(offset_));
  EXPECT_GE(elapsed.count(), ForceWake::kRetryMaxUs);
}

TEST_P(TestForceWake, Release) {
  register_io_->mmio()->Write32(0xFFFFFFFF, status_offset_);

  // Verify timeout waiting for status
  auto start = std::chrono::high_resolution_clock::now();
  ASSERT_FALSE(forcewake_->Release(register_io_.get(), domain_));

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::micro> elapsed = end - start;

  EXPECT_EQ(0x00010000u, register_io_->mmio()->Read32(offset_));
  EXPECT_GE(elapsed.count(), ForceWake::kRetryMaxUs);
}

static constexpr uint32_t kGen9DeviceId = 0x5916;
static constexpr uint32_t kGen12DeviceId = 0x9A49;

INSTANTIATE_TEST_SUITE_P(
    TestForceWake, TestForceWake,
    testing::Values(TestParam{.domain = ForceWakeDomain::RENDER, .device_id = kGen12DeviceId},
                    TestParam{.domain = ForceWakeDomain::GEN9_MEDIA, .device_id = kGen9DeviceId},
                    TestParam{.domain = ForceWakeDomain::GEN12_VDBOX0,
                              .device_id = kGen12DeviceId}),
    [](testing::TestParamInfo<TestParam> info) {
      switch (info.param.domain) {
        case ForceWakeDomain::RENDER:
          return "RENDER";
        case ForceWakeDomain::GEN9_MEDIA:
          return "GEN9_MEDIA";
        case ForceWakeDomain::GEN12_VDBOX0:
          return "GEN12_VDBOX0";
        default:
          return "Unknown";
      }
    });
