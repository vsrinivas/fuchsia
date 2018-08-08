// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): The "category" mechanism is limiting but it's what we have
// at the moment.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_
#define GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_set>

#include <zircon/device/cpu-trace/cpu-perf.h>

namespace cpuperf_provider {

enum class TraceOption {
  // Collect data from the o/s.
  kOs,
  // Collect data from userspace.
  kUser,
  // Collect the PC value for each event that is its own timebase.
  kPc,
};

enum class CategoryGroup {
  // Options like os vs user.
  kOption,
  // The sampling mode and frequency.
  kSample,
  // Collection of architecturally defined fixed-purpose events.
  kFixedArch,
  // Collection of architecturally defined programmable events.
  kProgrammableArch,
  // Collection of model-specific fixed-purpose events.
  kFixedModel,
  // Collection of model-specific programmable events.
  kProgrammableModel,
};

using CategoryValue = uint32_t;

struct CategorySpec {
  const char* name;
  CategoryGroup group;
  // This is only used by kOption and kSample.
  CategoryValue value;
  size_t count;
  const cpuperf_event_id_t* events;
};

struct TimebaseSpec {
  const char* name;
  const cpuperf_event_id_t event;
};

// A data collection run is called a "trace".
// This records the user-specified configuration of the trace.
class TraceConfig final {
 public:
  TraceConfig() {}

  bool is_enabled() const { return is_enabled_; }

  bool trace_os() const { return trace_os_; }
  bool trace_user() const { return trace_user_; }
  bool trace_pc() const { return trace_pc_; }

  uint32_t sample_rate() const { return sample_rate_; }

  cpuperf_event_id_t timebase_event() const { return timebase_event_; }

  // Reset state so that nothing is traced.
  void Reset();

  void Update();

  // Return true if the configuration has changed.
  bool Changed(const TraceConfig& old) const;

  // Translate our representation of the configuration to the device's.
  bool TranslateToDeviceConfig(cpuperf_config_t* out_config) const;

  // Return a string representation of the config for error reporting.
  std::string ToString() const;

 private:
  bool ProcessCategories();
  bool ProcessTimebase();

  bool is_enabled_ = false;

  bool trace_os_ = false;
  bool trace_user_ = false;
  bool trace_pc_ = false;
  uint32_t sample_rate_ = 0;
  cpuperf_event_id_t timebase_event_ = CPUPERF_EVENT_ID_NONE;

  // Set of selected fixed + programmable categories.
  std::unordered_set<const CategorySpec*> selected_categories_;
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_CATEGORIES_H_
