// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_EVENTS_H_
#define GARNET_LIB_PERFMON_EVENTS_H_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

namespace perfmon {

// TODO(dje): Reconcile event SYMBOLs with event names.
// Ideally they should match, but there's also good reasons
// to keep them different (organization, and matching vendor docs).
// TODO(dje): Add missing event descriptions. See perfmon --list-events.

struct EventDetails {
  perfmon_event_id_t id;
  // All these pointers point to storage of static duration.
  const char* name;
  const char* readable_name;
  const char* description;
};

// At the outer level, events are grouped by model: The client selects the
// set of events that are available for the particular model being used.

class ModelEventManager {
 public:
  // Register events for |group_name| for model |model_name|.
  // |events| points to storage of static duration. |model_name,group_name| do
  // not need to point to storage of static duration.
  // This may be called multiple times to register more events for the same
  // model,group. Event names must all be unique, newer events don't replace
  // previously registered events of the same name.
  // This function is not thread-safe.
  static void RegisterEvents(const char* model_name, const char* group_name,
                             const EventDetails* events, size_t count);

  // This function is not thread-safe.
  static std::unique_ptr<ModelEventManager> Create(const std::string& model_name);

  using EventTable = std::vector<const EventDetails*>;

  struct GroupEvents {
    // Within each model's set of events, events are organized into groups:
    // arch, fixed, model, misc.
    std::string group_name;

    EventTable events;
  };

  using GroupTable = std::vector<GroupEvents>;

  const std::string& model_name() const { return model_name_; }

  // Look up the event details for event |id|.
  // Returns true if |id| is valid, otherwise false.
  // This function is thread-safe.
  // TODO(dje): Rename to LookupEventById.
  bool EventIdToEventDetails(perfmon_event_id_t id,
                             const EventDetails** out_details) const;

  // Look up the event details for event |event_name| in group |group_name|.
  // Returns true if |id| is valid, otherwise false.
  // This function is thread-safe.
  bool LookupEventByName(const char* group_name, const char* event_name,
                         const EventDetails** out_details) const;

  // Return set of all supported events.
  // The result is an unsorted vector of vectors, one vector of events per
  // group.
  // This function is thread-safe.
  GroupTable GetAllGroups() const;

  // For debugging.
  void Dump() const;

 private:
  ModelEventManager(const std::string& model_name,
                    const EventTable* arch_events,
                    const EventTable* fixed_events,
                    const EventTable* model_events,
                    const EventTable* misc_events);

  void DumpGroup(const char* name, const EventTable* events) const;

  const std::string model_name_;

  // These point to pre-constructed tables.
  // They are never null but the tables may be empty.
  const EventTable* const arch_events_;
  const EventTable* const fixed_events_;
  const EventTable* const model_events_;
  const EventTable* const misc_events_;
};

// Pass this to |ModelEventManager::Create()| to get the default model for the
// current system.
// Returns "" if the default model is unknown (e.g., on unsupported arch).
std::string GetDefaultModelName();

// Return the number of events in |config|.
// This function is thread-safe.
size_t GetConfigEventCount(const perfmon_ioctl_config_t& config);

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_EVENTS_H_
