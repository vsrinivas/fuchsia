// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_POLICY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_POLICY_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/time.h>

#include <vector>

namespace media::audio {

class AudioPolicy {
 public:
  struct Rule {
    fuchsia::media::Usage active;
    fuchsia::media::Usage affected;
    fuchsia::media::Behavior behavior;
  };

  struct IdlePowerOptions {
    // If this value is nullopt, the entire "power-down idle outputs" policy is disabled.
    std::optional<zx::duration> idle_countdown_duration;

    // Outputs are enabled at driver-start. When this value is nullopt, outputs remain enabled and
    // ready indefinitely, until they are targeted by a render stream.
    std::optional<zx::duration> startup_idle_countdown_duration;

    // If true, all ultrasonic-capable channels will be enabled/disabled as an intact set.
    // Else, ultrasonic content requires only the FIRST ultrasonic-capable channel to be enabled.
    //
    // Relevant only for devices with more than one ultrasonic-capable channel, this is primarily
    // needed for devices with multiple channels that touch both audible AND ultrasonic ranges.
    // (other ultrasonic-capable channels may still remain enabled, to support audible frequencies)
    bool use_all_ultrasonic_channels = true;
  };

  AudioPolicy() = default;

  explicit AudioPolicy(std::vector<Rule> rules, IdlePowerOptions options)
      : rules_(std::move(rules)), idle_power_options_(options) {}

  const std::vector<Rule>& rules() const { return rules_; }
  const IdlePowerOptions& idle_power_options() const { return idle_power_options_; }

 private:
  std::vector<Rule> rules_;
  IdlePowerOptions idle_power_options_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_AUDIO_POLICY_H_
