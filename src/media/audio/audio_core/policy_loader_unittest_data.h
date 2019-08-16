// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_UNITTEST_DATA_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_UNITTEST_DATA_H_

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

// Examples of valid configs.
// A config with no rules specified is valid.
constexpr char empty_rules_json[] = R"JSON({"audio_policy_rules": []})JSON";

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
}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_POLICY_LOADER_UNITTEST_DATA_H_
