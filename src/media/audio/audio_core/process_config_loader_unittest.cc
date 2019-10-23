// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"

namespace media::audio {
namespace {

constexpr char kTestVolumeCurveFilename[] = "/tmp/volume_curve.json";

TEST(ProcessConfigLoaderTest, LoadVolumeCurve) {
  static std::string kTestVolumeCurve =
      R"JSON({
    "volume_curve": [
      {
          "level": 0.0,
          "db": -160.0
      },
      {
          "level": 1.0,
          "db": 0.0
      }
    ]
  })JSON";
  ASSERT_TRUE(
      files::WriteFile(kTestVolumeCurveFilename, kTestVolumeCurve.data(), kTestVolumeCurve.size()));

  const auto maybe_curve = ProcessConfigLoader::LoadVolumeCurveFromDisk(kTestVolumeCurveFilename);
  ASSERT_TRUE(maybe_curve.has_value());

  const auto& curve = maybe_curve.value();
  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.0), -160.0);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(1.0), 0.0);
}

TEST(ProcessConfigLoaderTest, NulloptOnMissingVolumeCurve) {
  const auto maybe_curve = ProcessConfigLoader::LoadVolumeCurveFromDisk("not-present-file");
  ASSERT_FALSE(maybe_curve.has_value());
}

}  // namespace
}  // namespace media::audio
