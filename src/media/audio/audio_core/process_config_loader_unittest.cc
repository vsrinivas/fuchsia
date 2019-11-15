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

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithRoutingPolicy) {
  static const std::string kConfigWithRoutingPolicy =
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
    "routing_policy": {
      "device_profiles": [
        {
          "device_id" : "34384e7da9d52c8062a9765baeb6053a",
          "supported_output_stream_types": [
            "media",
            "interruption",
            "background",
            "communications"
          ],
          "eligible_for_loopback": true
        },
        {
          "device_id": "*",
          "supported_output_stream_types": ["media", "system_agent"],
          "eligible_for_loopback": false
        }
      ]
    }
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  const audio_stream_unique_id_t expected_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                         0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                         0x05, 0x3a}};
  const audio_stream_unique_id_t unknown_id = {.data = {0x32, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x81, 0x42, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x22, 0x3a}};

  const auto process_config = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(process_config);

  using fuchsia::media::AudioRenderUsage;
  auto& config = process_config->routing_config();

  EXPECT_TRUE(config.device_profile(expected_id).supports_usage(AudioRenderUsage::MEDIA));
  EXPECT_TRUE(config.device_profile(expected_id).supports_usage(AudioRenderUsage::INTERRUPTION));
  EXPECT_FALSE(config.device_profile(expected_id).supports_usage(AudioRenderUsage::SYSTEM_AGENT));

  EXPECT_FALSE(config.device_profile(unknown_id).supports_usage(AudioRenderUsage::INTERRUPTION));
  EXPECT_TRUE(config.device_profile(unknown_id).supports_usage(AudioRenderUsage::MEDIA));

  EXPECT_TRUE(config.device_profile(expected_id).eligible_for_loopback());
  EXPECT_FALSE(config.device_profile(unknown_id).eligible_for_loopback());
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithRoutingPolicyNoDefault) {
  static const std::string kConfigWithRoutingPolicy =
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
    "routing_policy": {
      "device_profiles": [
        {
          "device_id" : "34384e7da9d52c8062a9765baeb6053a",
          "supported_output_stream_types": [
            "media",
            "interruption",
            "background",
            "communications",
            "system_agent"
          ],
          "eligible_for_loopback": true
        }
      ]
    }
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  const audio_stream_unique_id_t unknown_id = {.data = {0x32, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x81, 0x42, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x22, 0x3a}};

  const auto process_config = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(process_config);

  using fuchsia::media::AudioRenderUsage;
  auto& config = process_config->routing_config();

  EXPECT_TRUE(config.device_profile(unknown_id).supports_usage(AudioRenderUsage::INTERRUPTION));
  EXPECT_TRUE(config.device_profile(unknown_id).supports_usage(AudioRenderUsage::MEDIA));

  EXPECT_TRUE(config.device_profile(unknown_id).eligible_for_loopback());
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithRoutingPolicyInsufficientCoverage) {
  static const std::string kConfigWithRoutingPolicy =
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
    "routing_policy": {
      "device_profiles": [
        {
          "device_id" : "34384e7da9d52c8062a9765baeb6053a",
          "supported_output_stream_types": [
            "media",
            "interruption",
            "system_agent"
          ],
          "eligible_for_loopback": true
        }
      ]
    }
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  ASSERT_DEATH(ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename), "");
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
  syslog::InitLogger({"process_config_loader_test"});

  static const std::string kConfigWithoutVolumeCurve = R"JSON({  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithoutVolumeCurve.data(),
                               kConfigWithoutVolumeCurve.size()));

  ASSERT_DEATH(ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename), "");
}

TEST(ProcessConfigLoaderTest, RejectConfigWithUnknownKeys) {
  syslog::InitLogger({"process_config_loader_test"});

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

  ASSERT_DEATH(ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename), "");
}

}  // namespace
}  // namespace media::audio
