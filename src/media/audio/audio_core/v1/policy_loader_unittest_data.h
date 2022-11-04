// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_UNITTEST_DATA_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_UNITTEST_DATA_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/enum.h>

namespace media::audio::test {
// Examples of invalid configs.

// This config is missing 'audio_policy_rules'
constexpr char no_rules[] = R"JSON({"useless_key": 1.0})JSON";

constexpr char rules_not_array[] = R"JSON({"audio_policy_rules": 1.0})JSON";

constexpr char rules_array_not_rules[] = R"JSON({"audio_policy_rules": [ 1.0 ]})JSON";

constexpr char no_active[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "affected": {"render_usage":"MEDIA"},
          "behavior": "DUCK"
        }
      ]
    }
)JSON";

constexpr char no_affected[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "behavior": "DUCK"
        }
      ]
    }
)JSON";

constexpr char no_behavior[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"}
        }
      ]
    }
)JSON";

constexpr char invalid_renderusage[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"INVALID"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "DUCK"
        }
      ]
    }
)JSON";

constexpr char invalid_captureusage[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"capture_usage":"INVALID"},
          "behavior": "DUCK"
        }
      ]
    }
)JSON";

constexpr char invalid_behavior[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "INVALID"
        }
      ]
    }
)JSON";

constexpr char negative_countdown[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "NONE"
        }
      ],
      "idle_countdown_milliseconds": -1000
    }
)JSON";

constexpr char invalid_countdown[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "NONE"
        }
      ],
      "startup_idle_countdown_milliseconds": "string_not_integer",
    }
)JSON";

constexpr char invalid_channels[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "NONE"
        }
      ],
      "use_all_ultrasonic_channels": 0
    }
)JSON";

// Examples of valid configs.
// A config with no rules specified is valid.
constexpr char empty_rules_json[] = R"JSON({"audio_policy_rules": []})JSON";

// A config with no rules and one idle option specified is valid.
constexpr char empty_rules_plus_idle_json[] =
    R"JSON({"audio_policy_rules": [], "idle_countdown_milliseconds": 0})JSON";

// Make sure we don't error out if the json contains keys we don't care about.
constexpr char ignored_key[] = R"JSON({"useless_key": 1.0, "audio_policy_rules": []})JSON";

constexpr char render_render[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "NONE"
        }
      ]
    }
)JSON";

constexpr char render_capture[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"MEDIA"},
          "affected": {"capture_usage":"BACKGROUND"},
          "behavior": "NONE"
        }
      ]
    }
)JSON";

constexpr char capture_render[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"capture_usage":"BACKGROUND"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "NONE"
        }
      ]
    }
)JSON";

constexpr char capture_capture[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"capture_usage":"BACKGROUND"},
          "affected": {"capture_usage":"BACKGROUND"},
          "behavior": "NONE"
        }
      ]
    }
)JSON";

// Some static asserts that document the values we used to generate the JSON blob below with. If
// these fail we'll want to update the corresponding test data.
static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND) == 0);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA) == 1);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION) == 2);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT) == 3);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION) == 4);
static_assert(fuchsia::media::RENDER_USAGE_COUNT == 5);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::BACKGROUND) == 0);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::FOREGROUND) == 1);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT) == 2);
static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::COMMUNICATION) == 3);
static_assert(fuchsia::media::CAPTURE_USAGE_COUNT == 4);
static_assert(fidl::ToUnderlying(fuchsia::media::Behavior::NONE) == 0);
static_assert(fidl::ToUnderlying(fuchsia::media::Behavior::DUCK) == 1);
static_assert(fidl::ToUnderlying(fuchsia::media::Behavior::MUTE) == 2);

constexpr char contains_all_usages_and_behaviors[] = R"JSON(
    {
      "audio_policy_rules": [
        {
          "active": {"render_usage":"BACKGROUND"},
          "affected": {"render_usage":"MEDIA"},
          "behavior": "DUCK"
        },
        {
          "active": {"render_usage":"INTERRUPTION"},
          "affected": {"render_usage":"SYSTEM_AGENT"},
          "behavior": "MUTE"
        },
        {
          "active": {"render_usage":"COMMUNICATION"},
          "affected": {"capture_usage":"BACKGROUND"},
          "behavior": "NONE"
        },
        {
          "active": {"capture_usage":"FOREGROUND"},
          "affected": {"capture_usage":"SYSTEM_AGENT"},
          "behavior": "DUCK"
        },
        {
          "active": {"capture_usage":"SYSTEM_AGENT"},
          "affected": {"capture_usage":"COMMUNICATION"},
          "behavior": "DUCK"
        }
      ]
    }
)JSON";
}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_POLICY_LOADER_UNITTEST_DATA_H_
