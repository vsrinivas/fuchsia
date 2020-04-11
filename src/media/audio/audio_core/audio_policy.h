// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_POLICY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_POLICY_H_

#include <fuchsia/media/cpp/fidl.h>

#include <vector>

namespace media::audio {

class AudioPolicy {
 public:
  struct Rule {
    fuchsia::media::Usage active;
    fuchsia::media::Usage affected;
    fuchsia::media::Behavior behavior;
  };

  AudioPolicy() = default;

  explicit AudioPolicy(std::vector<Rule> rules) : rules_(std::move(rules)) {}

  const std::vector<Rule>& rules() const { return rules_; }

 private:
  std::vector<Rule> rules_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_POLICY_H_
