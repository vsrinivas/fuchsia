// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/config.h"

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace perfmon {

std::string Config::StatusToString(Status status) {
  switch (status) {
  case Status::OK:
    return "OK";
  case Status::MAX_EVENTS:
    return "MAX_EVENTS";
  case Status::INVALID_ARGS:
    return "INVALID_ARGS";
  }
}

void Config::Reset() {
  events_.clear();
}

Config::Status Config::AddEvent(
    EventId event, EventRate rate, uint32_t flags) {
  if (events_.size() == kMaxNumEvents) {
    return Status::MAX_EVENTS;
  }
  if (rate == 0 && (flags & Config::kNonZeroRateOnlyFlags) != 0) {
    return Status::INVALID_ARGS;
  }
  events_.emplace(EventConfig{event, rate, flags});
  return Status::OK;
}

size_t Config::GetEventCount() const {
  return events_.size();
}

CollectionMode Config::GetMode() const {
  for (const auto& event : events_) {
    // If any event is doing sampling, then we're in "sample mode".
    if (event.rate != 0) {
      return CollectionMode::kSample;
    }
  }
  return CollectionMode::kTally;
}

void Config::IterateOverEvents(IterateFunc func) const {
  for (const auto& event : events_) {
    func(event);
  }
}

static std::string EventConfigToString(Config::EventConfig event) {
  return fxl::StringPrintf("event 0x%x, rate %u, flags 0x%x",
                           event.event, event.rate, event.flags);
}

std::string Config::ToString() {
  std::string result;
  for (const auto& event : events_) {
    if (!result.empty()) {
      result += "; ";
    }
    result += EventConfigToString(event);
  }
  return result;
}

static void ToIoctlEvent(const Config::EventConfig& event, size_t index,
                         perfmon_ioctl_config_t* out_config) {
  out_config->events[index].event = event.event;
  out_config->events[index].rate = event.rate;
  if (event.flags & Config::kFlagOs) {
    out_config->events[index].flags |= PERFMON_CONFIG_FLAG_OS;
  }
  if (event.flags & Config::kFlagUser) {
    out_config->events[index].flags |= PERFMON_CONFIG_FLAG_USER;
  }
  if (event.flags & Config::kFlagPc) {
    out_config->events[index].flags |= PERFMON_CONFIG_FLAG_PC;
  }
  if (event.flags & Config::kFlagTimebase) {
    out_config->events[index].flags |= PERFMON_CONFIG_FLAG_TIMEBASE0;
  }
  if (event.flags & Config::kFlagLastBranch) {
    out_config->events[index].flags |= PERFMON_CONFIG_FLAG_LAST_BRANCH;
  }
}

namespace internal {

Config::Status PerfmonToIoctlConfig(const Config& config,
                                    perfmon_ioctl_config_t* out_config) {
  *out_config = {};
  FXL_DCHECK(config.GetEventCount() <= kMaxNumEvents);

  // There can be only one timebase event.
  bool have_timebase_event = false;
  size_t i = 0;
  Config::Status status = Config::Status::OK;

  // The ioctl takes the timebase in event #0, do that one first.
  // The timebase event is the event with kFlagTimebase and a non-zero rate.
  config.IterateOverEvents([&](const Config::EventConfig& event) {
    if ((event.flags & Config::kFlagTimebase) && event.rate != 0) {
      if (have_timebase_event) {
        status = Config::Status::INVALID_ARGS;
        return;
      }
      have_timebase_event = true;
      ToIoctlEvent(event, i, out_config);
      ++i;
    }
  });
  if (status != Config::Status::OK) {
    return status;
  }

  config.IterateOverEvents([&](const Config::EventConfig& event) {
    if (!((event.flags & Config::kFlagTimebase) && event.rate != 0)) {
      ToIoctlEvent(event, i, out_config);
      ++i;
    }
  });

  return Config::Status::OK;
}

}  // namespace internal

}  // namespace perfmon
