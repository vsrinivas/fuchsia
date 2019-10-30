// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config_loader.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"

namespace media::audio {
namespace {

constexpr char kTestAudioCoreConfigFilename[] = "/tmp/audio_core_config.json";

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithOnlyVolumeCurve) {
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
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithVolumeCurve.data(),
                               kConfigWithVolumeCurve.size()));

  const auto config = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(config);
  EXPECT_FLOAT_EQ(config->default_volume_curve().VolumeToDb(0.0), -160.0);
  EXPECT_FLOAT_EQ(config->default_volume_curve().VolumeToDb(1.0), 0.0);
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithEffects) {
  static const std::string kConfigWithEffects =
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
    ],
    "pipeline": {
      "_comment": "Just ignore me",
      "output_streams": [
        {
          "streams": ["media"],
          "name": "media",
          "effects": [
            {
              "lib": "libfoo.so",
              "name": "effect1",
              "config": {
                "some_config": 0
              }
            },
            {
              "lib": "libbar.so",
              "name": "effect2",
              "config": {
                "arg1": 55,
                "arg2": 3.14
              }
            }
          ]
        },
        {
          "streams": ["communications"],
          "name": "communications",
          "effects": [
            {
              "lib": "libbaz.so",
              "_comment": "Ignore me",
              "config": {
                "string_param": "some string value"
              }
            }
          ]
        }
      ],
      "mix": {
        "streams": [],
        "effects": [
          {
            "lib": "libfoo2.so",
            "name": "effect3"
          }
        ]
      },
      "linearize": {
        "streams": ["background", "system_agent", "media", "interruption"],
        "effects": [
          {
            "lib": "libbar2.so",
            "name": "linearize_effect",
            "_comment": "just a comment",
            "config": {
              "a": 123,
              "b": 456
            }
          }
        ]
      }
    }
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithEffects.data(),
                               kConfigWithEffects.size()));

  const auto config = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(config);

  ASSERT_EQ(2u, config->pipeline().GetOutputStreams().size());
  {  // output mix_group 1
    const auto& mix_group = config->pipeline().GetOutputStreams()[0];
    EXPECT_EQ("media", mix_group.name);
    EXPECT_EQ(1u, mix_group.input_streams.size());
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::MEDIA, mix_group.input_streams[0]);
    ASSERT_EQ(2u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libfoo.so", effect.lib_name);
      EXPECT_EQ("effect1", effect.effect_name);
      EXPECT_EQ("{\"some_config\":0}", effect.effect_config);
    }
    {
      const auto& effect = mix_group.effects[1];
      EXPECT_EQ("libbar.so", effect.lib_name);
      EXPECT_EQ("effect2", effect.effect_name);
      EXPECT_EQ("{\"arg1\":55,\"arg2\":3.14}", effect.effect_config);
    }
  }

  {  // output mix_group 2
    const auto& mix_group = config->pipeline().GetOutputStreams()[1];
    EXPECT_EQ("communications", mix_group.name);
    EXPECT_EQ(1u, mix_group.input_streams.size());
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::COMMUNICATION, mix_group.input_streams[0]);
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libbaz.so", effect.lib_name);
      EXPECT_EQ("", effect.effect_name);
      EXPECT_EQ("{\"string_param\":\"some string value\"}", effect.effect_config);
    }
  }

  {  // 'mix' mix_group
    const auto& mix_group = config->pipeline().GetMix();
    EXPECT_EQ("", mix_group.name);
    EXPECT_EQ(0u, mix_group.input_streams.size());
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libfoo2.so", effect.lib_name);
      EXPECT_EQ("effect3", effect.effect_name);
      EXPECT_EQ("", effect.effect_config);
    }
  }

  {  // 'linearize' mix_group
    const auto& mix_group = config->pipeline().GetLinearize();
    EXPECT_EQ("", mix_group.name);
    EXPECT_EQ(4u, mix_group.input_streams.size());
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::BACKGROUND, mix_group.input_streams[0]);
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, mix_group.input_streams[1]);
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::MEDIA, mix_group.input_streams[2]);
    EXPECT_EQ(fuchsia::media::AudioRenderUsage::INTERRUPTION, mix_group.input_streams[3]);
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libbar2.so", effect.lib_name);
      EXPECT_EQ("linearize_effect", effect.effect_name);
      EXPECT_EQ("{\"a\":123,\"b\":456}", effect.effect_config);
    }
  }
}

TEST(ProcessConfigLoaderTest, NulloptOnMissingConfig) {
  const auto config = ProcessConfigLoader::LoadProcessConfig("not-present-file");
  ASSERT_FALSE(config);
}

TEST(ProcessConfigLoaderTest, RejectConfigWithoutVolumeCurve) {
  static const std::string kConfigWithoutVolumeCurve = R"JSON({  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithoutVolumeCurve.data(),
                               kConfigWithoutVolumeCurve.size()));

  ASSERT_DEATH(ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename),
               "Schema validation error");
}

TEST(ProcessConfigLoaderTest, RejectConfigWithUnknownKeys) {
  static const std::string kConfigWithExtraKeys =
      R"JSON({
    "extra_key": 3,
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
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithExtraKeys.data(),
                               kConfigWithExtraKeys.size()));

  ASSERT_DEATH(ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename),
               "Schema validation error");
}

}  // namespace
}  // namespace media::audio
