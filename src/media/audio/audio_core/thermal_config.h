// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_

#include <vector>

namespace media::audio {

class ThermalConfig {
 public:
  // A state in a thermal policy for one target.
  class State {
   public:
    State(uint32_t trip_point, const char* config) : trip_point_(trip_point), config_(config) {}

    uint32_t trip_point() const { return trip_point_; }
    const std::string& config() const { return config_; }

   private:
    uint32_t trip_point_;
    std::string config_;
  };

  // A thermal policy for one target.
  class Entry {
   public:
    Entry(const char* target_name, std::vector<State> states)
        : target_name_(target_name), states_(states) {}

    const std::string& target_name() const { return target_name_; }
    const std::vector<State>& states() const { return states_; }

   private:
    std::string target_name_;
    std::vector<State> states_;
  };

  ThermalConfig(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  const std::vector<Entry>& entries() const { return entries_; }

 private:
  friend class ProcessConfigBuilder;

  std::vector<Entry> entries_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_
