// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_CONFIG_H_
#define GARNET_LIB_PERFMON_CONFIG_H_

#include <functional>
#include <stdint.h>
#include <string>
#include <unordered_set>

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

#include "garnet/lib/perfmon/types.h"

namespace perfmon {

// Description of what data to collect in a trace run.
// This is internally basically a copy of the FIDL struct, kept separate and
// filled in programmatically to not pass a FIDL dependency on to our clients.
class Config {
 public:
  // Data for one event. Passed to |IterateFunc|.
  struct EventConfig {
    // Event to collect data for.
    // The values are architecture specific ids.
    EventId event;

    // Sampling rate.
    // - If rate is non-zero then when the event gets this many hits data is
    //   collected (e.g., pc, time).
    //   The rate can be non-zero for counting based events only.
    // - If rate is zero then:
    //     If there is a timebase event then data for this event is collected
    //     when data for the timebase event is collected.
    //     Otherwise data for the event is collected once, when tracing stops.
    EventRate rate;

    // Flags for each event in |events|.
    // The values are |Config::kFlag*|.
    uint32_t flags;
  };

  // Callback for |IterateOverEvents()|.
  using IterateFunc = std::function<void(const EventConfig& event)>;

  // Bits for event flags.
  // TODO(dje): hypervisor, host/guest os/user
  static constexpr uint32_t kFlagMask = 0x1f;

  // Collect os data.
  // If neither |kFlagOs,KflagPc| are specified then both are collected.
  static constexpr uint32_t kFlagOs   = 0x1;

  // Collect userspace data.
  // If neither |kFlagOs,KflagPc| are specified then both are collected.
  static constexpr uint32_t kFlagUser = 0x2;

  // Collect aspace+pc values.
  static constexpr uint32_t kFlagPc   = 0x4;

  // If set then this event is used as the "timebase": data for events that
  // aren't their own time base is collected when data for this event is
  // collected. Events that are their own timebase have a non-zero rate.
  // It is an error to have this set and have the rate be zero.
  // There can be only one "timebase" event.
  static constexpr uint32_t kFlagTimebase = 0x8;

  // Collect the available set of last branches.
  // Branch data is emitted as PERFMON_RECORD_LAST_BRANCH records.
  // This is only available when the underlying system supports it.
  // TODO(dje): Provide knob to specify how many branches.
  static constexpr uint32_t kFlagLastBranch = 0x10;

  // These flags may only be specified with a non-zero rate.
  static constexpr uint32_t kNonZeroRateOnlyFlags =
    kFlagPc + kFlagLastBranch + kFlagTimebase;

  enum class Status {
    OK,
    // An invalid argument of some kind.
    INVALID_ARGS,
    // No room for more events.
    MAX_EVENTS,
  };

  static std::string StatusToString(Status status);

  Config() = default;
  ~Config() = default;

  // Use the default default copy/move constructors.

  // Remove existing contents.
  void Reset();

  // Collect data for event |event|.
  // If |rate| is zero then |flags| may only contain |kFlagOs,kFlagUser|.
  // If |rate| is non-zero then |flags| may contain any valid combination.
  // If |flags| contains |kFlagTimebase| then events with a zero rate are
  // collected at the same time as this event. Only one event may be added with
  // |kFlagTimebase|.
  // A value of zero for |flags| is equivalent to |kFlagOs|kFlagUser|.
  Status AddEvent(EventId event, EventRate rate, uint32_t flags);

  // Return the number of events.
  size_t GetEventCount() const;

  // Return the "mode" of data collection.
  CollectionMode GetMode() const;

  // Call |func| for each event.
  // The iteration order is unspecified.
  void IterateOverEvents(IterateFunc func) const;

  // Return a string form of the configuration, for display purposes.
  // The order of appearance of events in the string is unspecified.
  std::string ToString();

 private:
  struct EventHash {
    inline std::size_t operator()(const EventConfig& event) {
        return event.event;
    }
  };
  struct EventEqual {
    inline bool operator()(const EventConfig& e1, const EventConfig& e2) {
        return e1.event == e2.event;
    }
  };

  // Each event may appear at most once.
  std::unordered_set<EventConfig, EventHash, EventEqual> events_;
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_CONFIG_H_
