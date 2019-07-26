// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_EVENT_REGISTRY_H_
#define GARNET_LIB_PERFMON_EVENT_REGISTRY_H_

#include <stddef.h>
#include <string>
#include <unordered_map>

#include "garnet/lib/perfmon/events.h"

namespace perfmon {
namespace internal {

struct ModelEvents {
  ModelEventManager::EventTable arch_events;
  ModelEventManager::EventTable fixed_events;
  ModelEventManager::EventTable model_events;
  ModelEventManager::EventTable misc_events;
};

class EventRegistry : public std::unordered_map<std::string, ModelEvents> {
 public:
  // Register the events for |model_name,group_name|.
  // |events| points to storage of static duration.
  void RegisterEvents(const char* model_name, const char* group_name, const EventDetails* events,
                      size_t count);
};

// Names of the various event groups.
constexpr const char kArchGroupName[] = "arch";
constexpr const char kFixedGroupName[] = "fixed";
constexpr const char kModelGroupName[] = "model";
constexpr const char kMiscGroupName[] = "misc";

// Return a pointer to |g_model_events|, initializing it first if necessary.
// TODO(dje): Allow client to keep own registry.
EventRegistry* GetGlobalEventRegistry();

// Register all models and their events for this build.
void RegisterCurrentArchEvents(EventRegistry* registry);

// Register all events for all Intel,Arm64 models.
// TODO(dje): Support registering just a specific model.
void RegisterAllIntelModelEvents(EventRegistry* registry);
void RegisterAllArm64ModelEvents(EventRegistry* registry);

}  // namespace internal
}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_EVENT_REGISTRY_H_
