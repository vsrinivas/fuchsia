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

  auto config_result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(config_result.is_ok());
  const auto config = config_result.take_value();
  EXPECT_FLOAT_EQ(config.default_volume_curve().VolumeToDb(0.0), -160.0);
  EXPECT_FLOAT_EQ(config.default_volume_curve().VolumeToDb(1.0), 0.0);
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "capture:loopback"
        ]
      },
      {
        "device_id": "*",
        "supported_stream_types": [
          "render:media",
          "render:system_agent"
        ],
        "independent_volume_control": true
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  const audio_stream_unique_id_t expected_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                         0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                         0x05, 0x3a}};
  const audio_stream_unique_id_t unknown_id = {.data = {0x32, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x81, 0x42, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x22, 0x3a}};

  const auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  auto& config = result.value().device_config();

  EXPECT_TRUE(config.output_device_profile(expected_id).supports_usage(RenderUsage::MEDIA));
  EXPECT_TRUE(config.output_device_profile(expected_id).supports_usage(RenderUsage::INTERRUPTION));
  EXPECT_FALSE(config.output_device_profile(expected_id).supports_usage(RenderUsage::SYSTEM_AGENT));

  EXPECT_FALSE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::INTERRUPTION));
  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::MEDIA));

  EXPECT_TRUE(config.output_device_profile(expected_id).eligible_for_loopback());
  EXPECT_FALSE(config.output_device_profile(unknown_id).eligible_for_loopback());

  EXPECT_FALSE(config.output_device_profile(expected_id).independent_volume_control());
  EXPECT_TRUE(config.output_device_profile(unknown_id).independent_volume_control());
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithRoutingMultipleDeviceIds) {
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
    "output_devices": [
      {
        "device_id" : ["34384e7da9d52c8062a9765baeb6053a", "34384e7da9d52c8062a9765baeb6053b" ],
        "supported_stream_types": [
          "render:media"
        ]
      },
      {
        "device_id" : "*",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  const audio_stream_unique_id_t expected_id1 = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                          0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                          0x05, 0x3a}};
  const audio_stream_unique_id_t expected_id2 = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                          0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                          0x05, 0x3b}};

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  auto& config = result.value().device_config();
  for (const auto& device_id : {expected_id1, expected_id2}) {
    EXPECT_TRUE(config.output_device_profile(device_id).supports_usage(RenderUsage::MEDIA));
    EXPECT_FALSE(config.output_device_profile(device_id).supports_usage(RenderUsage::INTERRUPTION));
    EXPECT_FALSE(config.output_device_profile(device_id).supports_usage(RenderUsage::BACKGROUND));
    EXPECT_FALSE(
        config.output_device_profile(device_id).supports_usage(RenderUsage::COMMUNICATION));
    EXPECT_FALSE(config.output_device_profile(device_id).supports_usage(RenderUsage::SYSTEM_AGENT));

    EXPECT_FALSE(config.output_device_profile(device_id).eligible_for_loopback());
    EXPECT_FALSE(config.output_device_profile(device_id).independent_volume_control());
  }
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "render:ultrasound",
          "capture:loopback"
        ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  const audio_stream_unique_id_t unknown_id = {.data = {0x32, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x81, 0x42, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x22, 0x3a}};

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  auto& config = result.value().device_config();

  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::MEDIA));
  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::INTERRUPTION));
  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::BACKGROUND));
  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::COMMUNICATION));
  EXPECT_TRUE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::SYSTEM_AGENT));
  EXPECT_FALSE(config.output_device_profile(unknown_id).supports_usage(RenderUsage::ULTRASOUND));

  EXPECT_TRUE(config.output_device_profile(unknown_id).eligible_for_loopback());
}

TEST(ProcessConfigLoaderTest, RejectConfigWithUnknownStreamTypes) {
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "render:invalid"
        ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), R"ERROR(Parse error: Schema validation error ({
    "enum": {
        "instanceRef": "#/output_devices/0/supported_stream_types/5",
        "schemaRef": "#/definitions/stream_type"
    }
}))ERROR");
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:system_agent",
          "capture:loopback"
        ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(),
            "Parse error: Failed to parse output device policies: No output to support usage "
            "RenderUsage::BACKGROUND");
}

TEST(ProcessConfigLoaderTest, AllowConfigWithoutUltrasound) {
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithRoutingPolicy.data(),
                               kConfigWithRoutingPolicy.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithOutputDriverGain) {
  static const std::string kConfigWithDriverGain =
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
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ],
        "driver_gain_db": -6.0
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithDriverGain.data(),
                               kConfigWithDriverGain.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  const audio_stream_unique_id_t expected_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                         0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                         0x05, 0x3a}};
  const audio_stream_unique_id_t unknown_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x05, 0x3b}};
  auto& config = result.value().device_config();
  EXPECT_FLOAT_EQ(config.output_device_profile(expected_id).driver_gain_db(), -6.0f);
  EXPECT_FLOAT_EQ(config.output_device_profile(unknown_id).driver_gain_db(), 0.0f);
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithInputDevices) {
  static const std::string kConfigWithInputDevices =
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
    "input_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "capture:background"
        ],
        "rate": 96000
      },
      {
        "device_id": "*",
        "rate": 24000
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithInputDevices.data(),
                               kConfigWithInputDevices.size()));

  const audio_stream_unique_id_t expected_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                         0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                         0x05, 0x3a}};
  const audio_stream_unique_id_t unknown_id = {.data = {0x32, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                        0x81, 0x42, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                        0x22, 0x3a}};

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  auto& config = result.value().device_config();

  EXPECT_EQ(config.input_device_profile(expected_id).rate(), 96000u);
  EXPECT_TRUE(config.input_device_profile(expected_id)
                  .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND)));
  EXPECT_FALSE(config.input_device_profile(expected_id)
                   .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::FOREGROUND)));
  EXPECT_FALSE(config.input_device_profile(expected_id)
                   .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)));
  EXPECT_FALSE(config.input_device_profile(expected_id)
                   .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::COMMUNICATION)));
  EXPECT_FALSE(config.input_device_profile(expected_id)
                   .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)));
  EXPECT_EQ(config.input_device_profile(unknown_id).rate(), 24000u);
  EXPECT_TRUE(config.input_device_profile(unknown_id)
                  .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND)));
  EXPECT_TRUE(config.input_device_profile(unknown_id)
                  .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::FOREGROUND)));
  EXPECT_TRUE(config.input_device_profile(unknown_id)
                  .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)));
  EXPECT_TRUE(config.input_device_profile(unknown_id)
                  .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::COMMUNICATION)));
  EXPECT_FALSE(config.input_device_profile(unknown_id)
                   .supports_usage(StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)));
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithEffects) {
  static const std::string kConfigWithEffects =
      R"JSON({
    "volume_curve": [
      { "level": 0.0, "db": -160.0 },
      { "level": 1.0, "db": 0.0 }
    ],
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ],
        "pipeline": {
          "streams": [
            "render:background",
            "render:system_agent",
            "render:media",
            "render:interruption"
          ],
          "output_rate": 96000,
          "effects": [
            {
              "lib": "libbar2.so",
              "effect": "linearize_effect",
              "name": "instance_name",
              "_comment": "just a comment",
              "config": {
                "a": 123,
                "b": 456
              }
            }
          ],
          "inputs": [
            {
              "streams": [],
              "loopback": true,
              "output_rate": 48000,
              "effects": [
                {
                  "lib": "libfoo2.so",
                  "effect": "effect3"
                }
              ],
              "inputs": [
                {
                  "streams": [
                    "render:media"
                  ],
                  "name": "media",
                  "effects": [
                    {
                      "lib": "libfoo.so",
                      "effect": "effect1",
                      "config": {
                        "some_config": 0
                      }
                    },
                    {
                      "lib": "libbar.so",
                      "effect": "effect2",
                      "config": {
                        "arg1": 55,
                        "arg2": 3.14
                      }
                    }
                  ]
                },
                {
                  "streams": [
                    "render:communications"
                  ],
                  "name": "communications",
                  "effects": [
                    {
                      "lib": "libbaz.so",
                      "effect": "baz",
                      "_comment": "Ignore me",
                      "config": {
                        "string_param": "some string value"
                      }
                    }
                  ]
                }
              ]
            }
          ]
        }
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithEffects.data(),
                               kConfigWithEffects.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  const audio_stream_unique_id_t device_id = {.data = {0x34, 0x38, 0x4e, 0x7d, 0xa9, 0xd5, 0x2c,
                                                       0x80, 0x62, 0xa9, 0x76, 0x5b, 0xae, 0xb6,
                                                       0x05, 0x3a}};
  const auto& config = result.value();
  const auto& root =
      config.device_config().output_device_profile(device_id).pipeline_config().root();
  {  // 'linearize' mix_group
    const auto& mix_group = root;
    EXPECT_EQ("", mix_group.name);
    EXPECT_EQ(4u, mix_group.input_streams.size());
    EXPECT_EQ(RenderUsage::BACKGROUND, mix_group.input_streams[0]);
    EXPECT_EQ(RenderUsage::SYSTEM_AGENT, mix_group.input_streams[1]);
    EXPECT_EQ(RenderUsage::MEDIA, mix_group.input_streams[2]);
    EXPECT_EQ(RenderUsage::INTERRUPTION, mix_group.input_streams[3]);
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libbar2.so", effect.lib_name);
      EXPECT_EQ("linearize_effect", effect.effect_name);
      EXPECT_EQ("instance_name", effect.instance_name);
      EXPECT_EQ("{\"a\":123,\"b\":456}", effect.effect_config);
    }
    ASSERT_EQ(1u, mix_group.inputs.size());
    ASSERT_FALSE(mix_group.loopback);
    ASSERT_EQ(96000u, mix_group.output_rate);
  }

  const auto& mix = root.inputs[0];
  {  // 'mix' mix_group
    const auto& mix_group = mix;
    EXPECT_EQ("", mix_group.name);
    EXPECT_EQ(0u, mix_group.input_streams.size());
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libfoo2.so", effect.lib_name);
      EXPECT_EQ("effect3", effect.effect_name);
      EXPECT_EQ("", effect.effect_config);
    }
    ASSERT_EQ(2u, mix_group.inputs.size());
    ASSERT_TRUE(mix_group.loopback);
    ASSERT_EQ(48000u, mix_group.output_rate);
  }

  {  // output mix_group 1
    const auto& mix_group = mix.inputs[0];
    EXPECT_EQ("media", mix_group.name);
    EXPECT_EQ(1u, mix_group.input_streams.size());
    EXPECT_EQ(RenderUsage::MEDIA, mix_group.input_streams[0]);
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
    ASSERT_FALSE(mix_group.loopback);
    ASSERT_EQ(PipelineConfig::kDefaultMixGroupRate, mix_group.output_rate);
  }

  {  // output mix_group 2
    const auto& mix_group = mix.inputs[1];
    EXPECT_EQ("communications", mix_group.name);
    EXPECT_EQ(1u, mix_group.input_streams.size());
    EXPECT_EQ(RenderUsage::COMMUNICATION, mix_group.input_streams[0]);
    ASSERT_EQ(1u, mix_group.effects.size());
    {
      const auto& effect = mix_group.effects[0];
      EXPECT_EQ("libbaz.so", effect.lib_name);
      EXPECT_EQ("baz", effect.effect_name);
      EXPECT_EQ("{\"string_param\":\"some string value\"}", effect.effect_config);
    }
    ASSERT_FALSE(mix_group.loopback);
    ASSERT_EQ(PipelineConfig::kDefaultMixGroupRate, mix_group.output_rate);
  }
}

TEST(ProcessConfigLoaderTest, FileNotFound) {
  const auto result = ProcessConfigLoader::LoadProcessConfig("not-present-file");
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), "File does not exist");
}

TEST(ProcessConfigLoaderTest, RejectConfigWithoutVolumeCurve) {
  static const std::string kConfigWithoutVolumeCurve = R"JSON({  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithoutVolumeCurve.data(),
                               kConfigWithoutVolumeCurve.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(),
            R"ERROR(Parse error: Schema validation error ({
    "required": {
        "missing": [
            "volume_curve"
        ],
        "instanceRef": "#",
        "schemaRef": "#"
    }
}))ERROR");
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

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(),
            R"ERROR(Parse error: Schema validation error ({
    "additionalProperties": {
        "disallowed": "extra_key",
        "instanceRef": "#",
        "schemaRef": "#"
    }
}))ERROR");
}

TEST(ProcessConfigLoaderTest, RejectConfigWithMultipleLoopbackStages) {
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
    ],
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ],
        "pipeline": {
          "inputs": [
            {
              "streams": [
                "render:media",
                "render:interruption",
                "render:background",
                "render:system_agent"
              ],
              "loopback": true
            }, {
              "streams": [
                "render:communications"
              ],
              "loopback": true
            }
          ]
        }
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithVolumeCurve.data(),
                               kConfigWithVolumeCurve.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(
      result.error(),
      "Parse error: Failed to parse output device policies: More than 1 loopback stage specified");
}

TEST(ProcessConfigLoaderTest, RejectConfigWithoutLoopbackPointSpecified) {
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
    ],
    "output_devices": [
      {
        "device_id" : "34384e7da9d52c8062a9765baeb6053a",
        "supported_stream_types": [
          "render:media",
          "render:interruption",
          "render:background",
          "render:communications",
          "render:system_agent",
          "capture:loopback"
        ],
        "pipeline": {
          "streams": [
            "render:media",
            "render:interruption",
            "render:background",
            "render:communications",
            "render:system_agent"
          ]
        }
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithVolumeCurve.data(),
                               kConfigWithVolumeCurve.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(),
            "Parse error: Failed to parse output device policies: Device supports loopback but no "
            "loopback point specified");
}

TEST(ProcessConfigLoaderTest, LoadProcessConfigWithThermalPolicy) {
  static const std::string kConfigWithThermalPolicy =
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
    "thermal_policy" : [
      {
          "target_name": "target name 0",
          "states": [
            {
              "trip_point": 50,
              "config": {
                "value": "config 0 50"
              }
            }
          ]
      },
      {
          "target_name": "target name 1",
          "states": [
            {
              "trip_point": 25,
              "config": {
                "value": "config 1 25"
              }
            },
            {
              "trip_point": 50,
              "config": {
                "value": "config 1 50"
              }
            },
            {
              "trip_point": 75,
              "config": {
                "value": "config 1 75"
              }
            }
          ]
      }
    ]
  })JSON";
  ASSERT_TRUE(files::WriteFile(kTestAudioCoreConfigFilename, kConfigWithThermalPolicy.data(),
                               kConfigWithThermalPolicy.size()));

  auto result = ProcessConfigLoader::LoadProcessConfig(kTestAudioCoreConfigFilename);
  ASSERT_TRUE(result.is_ok());

  auto config = result.value();
  EXPECT_EQ(2u, config.thermal_config().entries().size());

  auto& entry0 = config.thermal_config().entries()[0];
  EXPECT_EQ("target name 0", entry0.target_name());
  EXPECT_EQ(1u, entry0.states().size());
  EXPECT_EQ(50u, entry0.states()[0].trip_point());
  EXPECT_EQ("{\"value\":\"config 0 50\"}", entry0.states()[0].config());

  auto& entry1 = config.thermal_config().entries()[1];
  EXPECT_EQ("target name 1", entry1.target_name());
  EXPECT_EQ(3u, entry1.states().size());
  EXPECT_EQ(25u, entry1.states()[0].trip_point());
  EXPECT_EQ("{\"value\":\"config 1 25\"}", entry1.states()[0].config());
  EXPECT_EQ(50u, entry1.states()[1].trip_point());
  EXPECT_EQ("{\"value\":\"config 1 50\"}", entry1.states()[1].config());
  EXPECT_EQ(75u, entry1.states()[2].trip_point());
  EXPECT_EQ("{\"value\":\"config 1 75\"}", entry1.states()[2].config());
}

}  // namespace
}  // namespace media::audio
