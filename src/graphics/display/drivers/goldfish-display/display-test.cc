// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "display.h"

#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/ddk/device.h>
#include <stdio.h>
#include <zircon/pixelformat.h>

#include <array>
#include <memory>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "src/graphics/display/drivers/goldfish-display/third_party/aosp/hwcomposer.h"
#include "zircon/system/public/zircon/pixelformat.h"

namespace goldfish {

namespace {
constexpr size_t kNumDisplays = 2;
constexpr size_t kMaxLayerCount = 3;  // This is the max size of layer array.
}  // namespace

class GoldfishDisplayTest : public zxtest::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  // Expose private classes defined in |Display| to the tests.
  using ColorBuffer = Display::ColorBuffer;
  using Swapchain = Display::Swapchain;

  // Expose private method to the tests.
  hwc::ComposeDeviceV2 CreateComposeDevice(size_t display_id, const std::list<layer_t>& layers,
                                           const ColorBuffer& target) const {
    return display_->CreateComposeDevice(display_->devices_[display_id], layers, target);
  }

 protected:
  std::array<std::array<layer_t, kMaxLayerCount>, kNumDisplays> layer_ = {};
  std::array<std::array<layer_t*, kMaxLayerCount>, kNumDisplays> layer_ptrs_ = {};

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
    for (size_t j = 0; j < kMaxLayerCount; j++) {
      layer_ptrs_[i][j] = &layer_[i][j];
    }
    configs_ptrs_[i] = &configs_[i];
    results_ptrs_[i] = &results_[i][0];
    configs_[i].display_id = i + 1;
    configs_[i].layer_list = layer_ptrs_[i].data();
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
  // Ensure we won't fail correctly if layers more than 1
  for (size_t i = 0; i < kNumDisplays; i++) {
    configs_[i].layer_count = kMaxLayerCount;
    for (size_t j = 0; j < kMaxLayerCount; j++) {
      configs_[i].layer_list[j]->type = LAYER_TYPE_PRIMARY;
    }
  }

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);
  for (size_t i = 0; i < kNumDisplays; i++) {
    EXPECT_EQ(0, result_count_[i]);
  }
}

TEST_F(GoldfishDisplayTest, CheckConfigLayerColor) {
  // Valid pixel format
  constexpr size_t kValidDisplayId = 0;
  configs_[kValidDisplayId].layer_list[0]->type = LAYER_TYPE_COLOR;
  configs_[kValidDisplayId].layer_list[0]->cfg.color.format = ZX_PIXEL_FORMAT_RGB_x888;

  // Invalid pixel format
  constexpr size_t kInvalidDisplayId = 1;
  configs_[kInvalidDisplayId].layer_list[0]->type = LAYER_TYPE_COLOR;
  configs_[kInvalidDisplayId].layer_list[0]->cfg.color.format = ZX_PIXEL_FORMAT_NV12;

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);

  EXPECT_EQ(0, result_count_[kValidDisplayId]);

  EXPECT_EQ(1, result_count_[kInvalidDisplayId]);
  EXPECT_EQ(CLIENT_USE_PRIMARY, results_[kInvalidDisplayId][0] & CLIENT_USE_PRIMARY);
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
    EXPECT_EQ(0, result_count_[i]);
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
    EXPECT_EQ(0, result_count_[i]);
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
    EXPECT_EQ(0, result_count_[i]);
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
    configs_[i].layer_list[0]->cfg.primary.alpha_layer_val = 0.5f;
  }

  // Valid alpha mode
  constexpr size_t kValidDisplayId = 0;
  configs_[kValidDisplayId].layer_list[0]->cfg.primary.alpha_mode = ALPHA_PREMULTIPLIED;

  // Invalid alpha mode
  constexpr size_t kInvalidDisplayId = 1;
  configs_[kInvalidDisplayId].layer_list[0]->cfg.primary.alpha_mode = ALPHA_HW_MULTIPLY;

  auto res = display_->DisplayControllerImplCheckConfiguration(
      const_cast<const display_config_t**>(configs_ptrs_.data()), kNumDisplays,
      results_ptrs_.data(), result_count_.data());
  EXPECT_OK(res);

  EXPECT_EQ(0, result_count_[kValidDisplayId]);

  EXPECT_EQ(1, result_count_[kInvalidDisplayId]);
  EXPECT_EQ(CLIENT_ALPHA, results_[kInvalidDisplayId][0]);
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
    EXPECT_EQ(0, result_count_[i]);
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
    EXPECT_EQ(0, results_[i][0] & CLIENT_FRAME_SCALE);
    EXPECT_EQ(0, results_[i][0] & CLIENT_SRC_FRAME);
    EXPECT_EQ(CLIENT_ALPHA, results_[i][0] & CLIENT_ALPHA);
    EXPECT_EQ(0, results_[i][0] & CLIENT_TRANSFORM);
    // TODO(payamm): Driver will pretend it supports color conversion for now
    // EXPECT_EQ(CLIENT_COLOR_CONVERSION, results_[i][0] & CLIENT_COLOR_CONVERSION);
  }
}

TEST_F(GoldfishDisplayTest, Swapchain) {
  constexpr size_t kNumColorBuffers = 2u;
  std::unique_ptr<ColorBuffer> color_buffers[kNumColorBuffers] = {
      std::make_unique<ColorBuffer>(),
      std::make_unique<ColorBuffer>(),
  };
  color_buffers[0]->id = 1;
  color_buffers[1]->id = 2;

  Swapchain swapchain;
  swapchain.Add(std::move(color_buffers[0]));
  swapchain.Add(std::move(color_buffers[1]));

  // Ensure that frame buffers are in correct order.
  auto requested_buffer_0 = swapchain.Request();
  auto requested_buffer_1 = swapchain.Request();
  EXPECT_EQ(requested_buffer_0->id, 1);
  EXPECT_EQ(requested_buffer_1->id, 2);

  auto requested_buffer_2 = swapchain.Request();
  EXPECT_EQ(requested_buffer_2, nullptr);

  // Ensure that caller always gets the first returned buffer.
  swapchain.Return(requested_buffer_1);
  swapchain.Return(requested_buffer_0);
  auto requested_buffer_3 = swapchain.Request();
  EXPECT_EQ(requested_buffer_3->id, 2);
}

TEST_F(GoldfishDisplayTest, ComposeCommand) {
  constexpr uint8_t kColor[] = {/* b */ 11, /* g */ 22, /* r */ 33, /* a */ 255};
  const ColorBuffer kFakeCursorBuffer = {
      .id = 1,
      .width = 30,
      .height = 30,
  };
  const ColorBuffer kFakePrimaryBuffer = {
      .id = 2,
      .width = 1024,
      .height = 768,
  };
  const ColorBuffer kFakeTargetBuffer = {
      .id = 3,
      .width = 1024,
      .height = 768,
  };
  constexpr size_t kOutputDisplayId = 1u;

  std::list<layer_t> layers = {
      layer_t{
          .type = LAYER_TYPE_COLOR,
          .z_index = 1,
          .cfg =
              {
                  .color =
                      {
                          .format = ZX_PIXEL_FORMAT_RGB_x888,
                          .color_list = kColor,
                          .color_count = 4,
                      },
              },
      },
      layer_t{.type = LAYER_TYPE_PRIMARY,
              .z_index = 2,
              .cfg = {.primary =
                          {
                              .image =
                                  {
                                      .width = kFakePrimaryBuffer.width,
                                      .height = kFakePrimaryBuffer.height,
                                      .handle = reinterpret_cast<uint64_t>(&kFakePrimaryBuffer),
                                  },
                              .alpha_mode = ALPHA_PREMULTIPLIED,
                              .alpha_layer_val = 0.5,
                              .transform_mode = FRAME_TRANSFORM_REFLECT_X,
                              .src_frame =
                                  {
                                      .x_pos = 0,
                                      .y_pos = 0,
                                      .width = kFakePrimaryBuffer.width,
                                      .height = kFakePrimaryBuffer.height,
                                  },
                              .dest_frame =
                                  {
                                      .x_pos = 0,
                                      .y_pos = 0,
                                      .width = kFakePrimaryBuffer.width,
                                      .height = kFakePrimaryBuffer.height,
                                  },
                          }}},
      layer_t{
          .type = LAYER_TYPE_CURSOR,
          .z_index = 999,
          .cfg =
              {
                  .cursor =
                      {
                          .image =
                              {
                                  .width = kFakeCursorBuffer.width,
                                  .height = kFakeCursorBuffer.height,
                                  .handle = reinterpret_cast<uint64_t>(&kFakeCursorBuffer),
                              },
                          .x_pos = 50,
                          .y_pos = 100,
                      },
              },
      },
  };

  auto compose_command = CreateComposeDevice(kOutputDisplayId, layers, kFakeTargetBuffer);
  ASSERT_EQ(compose_command.size(),
            sizeof(hwc::compose_device_v2) + 3 * sizeof(hwc::compose_layer));

  EXPECT_EQ(compose_command->version, 2u);
  EXPECT_EQ(compose_command->num_layers, 3u);
  EXPECT_EQ(compose_command->target_handle, kFakeTargetBuffer.id);

  // layer type correct
  EXPECT_EQ(compose_command->layers[0].compose_mode, hwc::Composition::kSolidColor);
  EXPECT_EQ(compose_command->layers[1].compose_mode, hwc::Composition::kDevice);
  EXPECT_EQ(compose_command->layers[2].compose_mode, hwc::Composition::kDevice);

  // layer handle correct
  EXPECT_EQ(compose_command->layers[0].cb_handle, 0);
  EXPECT_EQ(compose_command->layers[1].cb_handle, kFakePrimaryBuffer.id);
  EXPECT_EQ(compose_command->layers[2].cb_handle, kFakeCursorBuffer.id);

  // color correct
  EXPECT_EQ(compose_command->layers[0].color.b, kColor[0]);
  EXPECT_EQ(compose_command->layers[0].color.g, kColor[1]);
  EXPECT_EQ(compose_command->layers[0].color.r, kColor[2]);
}

}  // namespace goldfish
