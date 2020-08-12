// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_

#include <fuchsia/thermal/cpp/fidl.h>

#include <vector>

namespace media::audio {

// Represents the thermal policy configuration found in an audio_core configuration file.
//
// ThermalConfig is conceptually of the form [Entry(TripPoint, [StateTransition])]. When the
// outer list contains N entries, it specifies N+1 thermal states. Each Entry specifies
// the transitions in effect states that occur when its TripPoint is activated.
class ThermalConfig {
 public:
  using TripPoint = fuchsia::thermal::TripPoint;

  class StateTransition {
   public:
    StateTransition(const char* target_name, const char* config)
        : target_name_(target_name), config_(config) {}

    const std::string& target_name() const { return target_name_; }
    const std::string& config() const { return config_; }

   private:
    std::string target_name_;
    std::string config_;
  };

  class Entry {
   public:
    Entry(const TripPoint& trip_point, std::vector<StateTransition> state_transitions)
        : trip_point_(trip_point), state_transitions_(std::move(state_transitions)) {}

    const TripPoint& trip_point() const { return trip_point_; }
    const std::vector<StateTransition>& state_transitions() const { return state_transitions_; }

   private:
    TripPoint trip_point_;
    std::vector<StateTransition> state_transitions_;
  };

  explicit ThermalConfig(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  const std::vector<Entry>& entries() const { return entries_; }

 private:
  friend class ProcessConfigBuilder;

  std::vector<Entry> entries_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THERMAL_CONFIG_H_
