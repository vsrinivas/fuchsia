// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "display.h"

#include <stdio.h>

#include <array>
#include <memory>

#include <ddk/device.h>
#include <ddk/protocol/display/controller.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace goldfish {

namespace {
constexpr size_t kNumDisplays = 2;
constexpr size_t kMaxLayerCount = 3;  // This is the max size of layer array.
}  // namespace

class GoldfishDisplayTest : public zxtest::Test {
 public:
  void SetUp() override;
  void TearDown() override;
  std::array<std::array<layer_t, kMaxLayerCount>, kNumDisplays> layer_ = {};
  std::array<layer_t*, kNumDisplays> layer_ptrs = {};

  std::array<display_config_t, kNumDisplays> configs_ = {};
  std::array<display_config_t*, kNumDisplays> configs_ptrs_ = {};

  std::array<std::array<uint32_t, kMaxLayerCount>, kNumDisplays> results_ = {};
  std::array<uint32_t*, kNumDisplays> results_ptrs_ = {};

  std::array<size_t, kNumDisplays> result_count_ = {};
  std::unique_ptr<Display> display_ = {};
};

void GoldfishDisplayTest::SetUp() {
  display_ = std::make_unique<Display>(nullptr);

  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_ptrs_[i] = &configs_[i];
    layer_ptrs[i] = &layer_[i][0];
    results_ptrs_[i] = &results_[i][0];
    configs_[i].display_id = i + 1;
    configs_[i].layer_list = &layer_ptrs[i];
    configs_[i].layer_count = 1;
  }

  // Need CreateDevices and RemoveDevices to ensure we can test CheckConfiguration without any
  // dependency on proper driver binding/loading
  display_->CreateDevices(kNumDisplays);
}

void GoldfishDisplayTest::TearDown() { display_->RemoveDevices(); }

TEST_F(GoldfishDisplayTest, CheckConfigNoDisplay) {
  // Test No display
  uint32_t res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), 0, results_ptrs_.data(),
      result_count_.data());
  EXPECT_OK(res);
}

TEST_F(GoldfishDisplayTest, CheckConfigMultiLayer) {
  // ensure we fail correctly if layers more than 1
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_count = kMaxLayerCount;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t j = 0; j < kNumDisplays; j++) {
    EXPECT_EQ(kMaxLayerCount, result_count_[j]);
    EXPECT_EQ(CLIENT_MERGE_BASE, results_[j][0] & CLIENT_MERGE_BASE);
    for (unsigned i = 1; i < kMaxLayerCount; i++) {
      EXPECT_EQ(CLIENT_MERGE_SRC, results_[j][i]);
    }
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerColor) {
  // First create layer for each device
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->type = LAYER_TYPE_COLOR;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_USE_PRIMARY, results_[i][0] & CLIENT_USE_PRIMARY);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerCursor) {
  // First create layer for each device
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->type = LAYER_TYPE_CURSOR;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_USE_PRIMARY, results_[i][0] & CLIENT_USE_PRIMARY);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerPrimary) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
    configs_[i].layer_list[0]->cfg.primary.alpha_mode = 0;
    configs_[i].layer_list[0]->cfg.primary.transform_mode = 0;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(0, result_count_[i]);
    if (result_count_[i] != 0) {
      printf("Results: 0x%x\n", results_[i][0]);
    }
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerDestFrame) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 768,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_FRAME_SCALE, results_[i][0]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerSrcFrame) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 768,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_SRC_FRAME, results_[i][0]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerAlpha) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
    configs_[i].layer_list[0]->cfg.primary.alpha_mode = ALPHA_HW_MULTIPLY;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_ALPHA, results_[i][0]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerTransform) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
    configs_[i].layer_list[0]->cfg.primary.transform_mode = FRAME_TRANSFORM_REFLECT_X;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_TRANSFORM, results_[i][0]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerColorCoversion) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 1024,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
    configs_[i].cc_flags = COLOR_CONVERSION_POSTOFFSET;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(0, result_count_[i]);
    // TODO(payamm): For now, driver will pretend it supports color conversion
    // EXPECT_EQ(1, result_count_[i]);
    // EXPECT_EQ(CLIENT_COLOR_CONVERSION, results_[i][0]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigAllFeatures) {
  // First create layer for each device
  frame_t dest_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 768,
      .height = 768,
  };
  frame_t src_frame = {
      .x_pos = 0,
      .y_pos = 0,
      .width = 768,
      .height = 768,
  };
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_list[0]->cfg.primary.dest_frame = dest_frame;
    configs_[i].layer_list[0]->cfg.primary.src_frame = src_frame;
    configs_[i].layer_list[0]->cfg.primary.image.width = 1024;
    configs_[i].layer_list[0]->cfg.primary.image.height = 768;
    configs_[i].layer_list[0]->cfg.primary.alpha_mode = ALPHA_HW_MULTIPLY;
    configs_[i].layer_list[0]->cfg.primary.transform_mode = FRAME_TRANSFORM_ROT_180;
    configs_[i].cc_flags = COLOR_CONVERSION_POSTOFFSET;
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(1, result_count_[i]);
    EXPECT_EQ(CLIENT_FRAME_SCALE, results_[i][0] & CLIENT_FRAME_SCALE);
    EXPECT_EQ(CLIENT_SRC_FRAME, results_[i][0] & CLIENT_SRC_FRAME);
    EXPECT_EQ(CLIENT_ALPHA, results_[i][0] & CLIENT_ALPHA);

    EXPECT_EQ(CLIENT_TRANSFORM, results_[i][0] & CLIENT_TRANSFORM);
    // TODO(payamm): Driver will pretend it supports color conversion for now
    // EXPECT_EQ(CLIENT_COLOR_CONVERSION, results_[i][0] & CLIENT_COLOR_CONVERSION);
  }
}

}  // namespace goldfish
