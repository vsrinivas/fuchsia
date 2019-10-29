// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"

namespace media::audio {
namespace {

constexpr char kTestVolumeCurveFilename[] = "/tmp/volume_curve.json";

static const std::string kConfigWithVolumeCurve =
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

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithOnlyVolumeCurve) {
  ASSERT_TRUE(files::WriteFile(kTestVolumeCurveFilename, kConfigWithVolumeCurve.data(),
                               kConfigWithVolumeCurve.size()));

  const auto config = ProcessConfigLoader::LoadProcessConfig(kTestVolumeCurveFilename);
  ASSERT_TRUE(config);
  EXPECT_FLOAT_EQ(config->default_volume_curve().VolumeToDb(0.0), -160.0);
  EXPECT_FLOAT_EQ(config->default_volume_curve().VolumeToDb(1.0), 0.0);
}

TEST(ProcessConfigLoaderTest, NulloptOnMissingConfig) {
  const auto config = ProcessConfigLoader::LoadProcessConfig("not-present-file");
  ASSERT_FALSE(config);
}

}  // namespace
}  // namespace media::audio
