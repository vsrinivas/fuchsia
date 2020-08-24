// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "src/camera/drivers/controller/configs/sherlock/common_util.h"
#include "src/camera/drivers/controller/configs/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/configs/sherlock/sherlock_product_config.h"
#include "src/camera/drivers/controller/test/constants.h"

namespace camera {

namespace {

constexpr auto kStreamTypeFR = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
constexpr auto kStreamTypeDS = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION;
constexpr auto kStreamTypeML = fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
constexpr auto kStreamTypeVideo = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
constexpr auto kStreamTypeMonitoring = fuchsia::camera2::CameraStreamType::MONITORING;

TEST(ConfigTest, ExternalConfiguration) {
  auto sherlock_configs = ProductConfig::Create();
  auto external_configs = sherlock_configs->ExternalConfigs();
  auto internal_configs = sherlock_configs->InternalConfigs();

  EXPECT_EQ(external_configs.size(), SherlockConfigs::MAX);
  EXPECT_EQ(internal_configs.configs_info.size(), SherlockConfigs::MAX);
}

TEST(ConfigTest, InternalMonitorConfiguration) {
  auto sherlock_configs = ProductConfig::Create();
  auto internal_configs = sherlock_configs->InternalConfigs();
  auto monitor_config = internal_configs.configs_info.at(SherlockConfigs::MONITORING);
  auto fr = monitor_config.streams_info.at(0);
  auto ds = monitor_config.streams_info.at(1);
  EXPECT_EQ(monitor_config.streams_info.size(), 2u);

  // 1st Stream is FR.
  EXPECT_EQ(fr.input_stream_type, kStreamTypeFR);
  // 2nd Stream is DS.
  EXPECT_EQ(ds.input_stream_type, kStreamTypeDS);

  // FR Supported streams.
  EXPECT_EQ(fr.supported_streams.size(), 2u);
  EXPECT_EQ(fr.supported_streams[0].type, kStreamTypeFR | kStreamTypeML);
  EXPECT_EQ(fr.supported_streams[1].type, kStreamTypeDS | kStreamTypeML);
  // DS supported streams
  EXPECT_EQ(ds.supported_streams.size(), 1u);
  EXPECT_EQ(ds.supported_streams[0].type, kStreamTypeMonitoring);

  // DS graph validation.
  ASSERT_EQ(ds.child_nodes.size(), 1u);
  auto input_node = ds;
  auto gdc_ds_node = ds.child_nodes[0];
  ASSERT_EQ(gdc_ds_node.child_nodes.size(), 1u);
  auto ge2d_ds_node = gdc_ds_node.child_nodes[0];
  ASSERT_EQ(ge2d_ds_node.child_nodes.size(), 1u);
  auto output_ds_node = ge2d_ds_node.child_nodes[0];

  EXPECT_EQ(NodeType::kInputStream, input_node.type);
  EXPECT_EQ(NodeType::kGdc, gdc_ds_node.type);
  EXPECT_EQ(NodeType::kGe2d, ge2d_ds_node.type);
  EXPECT_EQ(NodeType::kOutputStream, output_ds_node.type);

  ASSERT_EQ(input_node.supported_streams.size(), 1u);
  ASSERT_EQ(gdc_ds_node.supported_streams.size(), 1u);
  ASSERT_EQ(ge2d_ds_node.supported_streams.size(), 1u);
  ASSERT_EQ(output_ds_node.supported_streams.size(), 1u);

  EXPECT_TRUE(gdc_ds_node.supported_streams.at(0).supports_dynamic_resolution);

  EXPECT_EQ(gdc_ds_node.supported_streams.at(0).type, kStreamTypeMonitoring);
  EXPECT_EQ(input_node.supported_streams.at(0).type, kStreamTypeMonitoring);
  EXPECT_EQ(ge2d_ds_node.supported_streams.at(0).type, kStreamTypeMonitoring);
  EXPECT_EQ(output_ds_node.supported_streams.at(0).type, kStreamTypeMonitoring);

  ASSERT_EQ(gdc_ds_node.gdc_info.config_type.size(), 3u);

  EXPECT_EQ(gdc_ds_node.gdc_info.config_type[0], GdcConfig::MONITORING_720p);
  EXPECT_EQ(gdc_ds_node.gdc_info.config_type[1], GdcConfig::MONITORING_480p);
  EXPECT_EQ(gdc_ds_node.gdc_info.config_type[2], GdcConfig::MONITORING_360p);

  EXPECT_EQ(ge2d_ds_node.ge2d_info.config_type, Ge2DConfig::GE2D_WATERMARK);
  EXPECT_EQ(ge2d_ds_node.in_place, true);

  EXPECT_EQ(output_ds_node.image_formats.size(), 3u);

  // FR graph validation.
  input_node = fr;
  ASSERT_EQ(fr.child_nodes.size(), 2u);
  auto output_fr_node = fr.child_nodes[0];
  gdc_ds_node = fr.child_nodes[1];
  ASSERT_EQ(gdc_ds_node.child_nodes.size(), 1u);
  output_ds_node = gdc_ds_node.child_nodes[0];

  EXPECT_EQ(NodeType::kInputStream, input_node.type);
  EXPECT_EQ(NodeType::kGdc, gdc_ds_node.type);
  EXPECT_EQ(NodeType::kOutputStream, output_fr_node.type);
  EXPECT_EQ(NodeType::kOutputStream, output_ds_node.type);

  ASSERT_EQ(input_node.supported_streams.size(), 2u);
  ASSERT_EQ(gdc_ds_node.supported_streams.size(), 1u);
  ASSERT_EQ(output_fr_node.supported_streams.size(), 1u);
  ASSERT_EQ(output_ds_node.supported_streams.size(), 1u);

  EXPECT_EQ(input_node.supported_streams.at(0).type, kStreamTypeFR | kStreamTypeML);
  EXPECT_EQ(input_node.supported_streams.at(1).type, kStreamTypeML | kStreamTypeDS);
  EXPECT_EQ(gdc_ds_node.supported_streams.at(0).type, kStreamTypeML | kStreamTypeDS);
  EXPECT_EQ(output_ds_node.supported_streams.at(0).type, kStreamTypeML | kStreamTypeDS);
  EXPECT_EQ(output_fr_node.supported_streams.at(0).type, kStreamTypeML | kStreamTypeFR);

  EXPECT_EQ(output_ds_node.image_formats.size(), 1u);
  EXPECT_EQ(output_fr_node.image_formats.size(), 1u);
}

TEST(ConfigTest, InternalVideoConferenceConfiguration) {
  auto sherlock_configs = ProductConfig::Create();
  auto internal_configs = sherlock_configs->InternalConfigs();
  auto video_config = internal_configs.configs_info.at(SherlockConfigs::VIDEO);

  EXPECT_EQ(video_config.streams_info.size(), 1u);
  // 1st stream is FR.
  EXPECT_EQ(video_config.streams_info.at(0).input_stream_type, kStreamTypeFR);

  // FR supported streams.
  EXPECT_EQ(video_config.streams_info.at(0).supported_streams.size(), 2u);
  EXPECT_EQ(video_config.streams_info.at(0).supported_streams[0].type,
            kStreamTypeVideo | kStreamTypeFR | kStreamTypeML);
  EXPECT_EQ(video_config.streams_info.at(0).supported_streams[1].type, kStreamTypeVideo);

  // FR graph validation.
  auto input_node = video_config.streams_info.at(0);
  ASSERT_EQ(input_node.child_nodes.size(), 1u);
  auto gdc1_node = input_node.child_nodes[0];
  auto ge2d_node = gdc1_node.child_nodes[1];
  ASSERT_EQ(gdc1_node.child_nodes.size(), 2u);
  auto gdc2_node = gdc1_node.child_nodes[0];
  ASSERT_EQ(gdc2_node.child_nodes.size(), 1u);
  auto output_ml_node = gdc2_node.child_nodes[0];
  auto output_video_node = ge2d_node.child_nodes[0];

  EXPECT_EQ(NodeType::kInputStream, input_node.type);
  EXPECT_EQ(NodeType::kGdc, gdc1_node.type);
  EXPECT_EQ(NodeType::kGdc, gdc2_node.type);
  EXPECT_EQ(NodeType::kGe2d, ge2d_node.type);
  EXPECT_EQ(NodeType::kOutputStream, output_ml_node.type);
  EXPECT_EQ(NodeType::kOutputStream, output_video_node.type);

  EXPECT_EQ(output_ml_node.image_formats.size(), 1u);
  EXPECT_EQ(output_video_node.image_formats.size(), 3u);

  EXPECT_TRUE(ge2d_node.supported_streams.at(0).supports_dynamic_resolution);
  EXPECT_TRUE(ge2d_node.supported_streams.at(0).supports_crop_region);
}

}  // namespace

}  // namespace camera
