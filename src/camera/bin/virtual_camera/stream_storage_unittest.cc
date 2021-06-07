// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/virtual_camera/stream_storage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/camera/lib/stream_utils/stream_constraints.h"

namespace {

TEST(StreamStorageTest, GetConfig_Empty) {
  camera::StreamStorage stream_storage_;
  fuchsia::camera2::hal::Config config = stream_storage_.GetConfig();

  EXPECT_THAT(config.stream_configs, ::testing::SizeIs(0));
}

TEST(StreamStorageTest, AddStreamConfigAndGetConfig) {
  camera::StreamStorage stream_storage_;

  camera::StreamConstraints constraints(fuchsia::camera2::CameraStreamType::MONITORING);
  constraints.AddImageFormat(100, 100, fuchsia::sysmem::PixelFormatType::NV12);

  stream_storage_.SetStreamConfigAtIndex(0, constraints.ConvertToStreamConfig());
  stream_storage_.SetStreamConfigAtIndex(1, constraints.ConvertToStreamConfig());

  fuchsia::camera2::hal::Config config = stream_storage_.GetConfig();
  EXPECT_THAT(config.stream_configs, ::testing::SizeIs(2));
  EXPECT_TRUE(fidl::Equals(config.stream_configs[0], constraints.ConvertToStreamConfig()));
  EXPECT_TRUE(fidl::Equals(config.stream_configs[1], constraints.ConvertToStreamConfig()));
}

TEST(StreamStorageTest, AddStreamConfigAndGetConfig_WithGaps) {
  camera::StreamStorage stream_storage_;

  camera::StreamConstraints constraints(fuchsia::camera2::CameraStreamType::MONITORING);
  constraints.AddImageFormat(100, 100, fuchsia::sysmem::PixelFormatType::NV12);

  stream_storage_.SetStreamConfigAtIndex(0, constraints.ConvertToStreamConfig());
  stream_storage_.SetStreamConfigAtIndex(3, constraints.ConvertToStreamConfig());

  fuchsia::camera2::hal::Config config = stream_storage_.GetConfig();
  EXPECT_THAT(config.stream_configs, ::testing::SizeIs(2));
  EXPECT_TRUE(fidl::Equals(config.stream_configs[0], constraints.ConvertToStreamConfig()));
  EXPECT_TRUE(fidl::Equals(config.stream_configs[1], constraints.ConvertToStreamConfig()));
}

}  // namespace
