// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/event-registry.h"
#include "garnet/lib/perfmon/events.h"

namespace perfmon {

// Tables of each model's registered events.
static internal::EventRegistry* g_model_events;

namespace internal {

EventRegistry* GetGlobalEventRegistry() {
  if (g_model_events == nullptr) {
    FXL_VLOG(2) << "Initializing model event registry";
    g_model_events = new internal::EventRegistry{};
  }
  return g_model_events;
}

}  // namespace internal

std::string GetDefaultModelName() {
  std::string model_name = "";
#ifdef __x86_64__
  model_name = "skylake";
#endif
#ifdef __aarch64__
  model_name = "armv8";
#endif
  return model_name;
}

void ModelEventManager::RegisterEvents(const char* model_name,
                                       const char* group_name,
                                       const EventDetails* events,
                                       size_t count) {
  internal::EventRegistry* registry = internal::GetGlobalEventRegistry();
  registry->RegisterEvents(model_name, group_name, events, count);
}

std::unique_ptr<ModelEventManager> ModelEventManager::Create(
    const std::string& model_name) {
  // For convenience, if no events have been registered yet, ensure the
  // current arch's events are registered.
  if (g_model_events == nullptr) {
    internal::EventRegistry* registry = internal::GetGlobalEventRegistry();
    internal::RegisterCurrentArchEvents(registry);
  }
  auto iter = g_model_events->find(model_name);
  if (iter == g_model_events->end()) {
    return nullptr;
  }

  auto model_event_manager =
    std::make_unique<ModelEventManager>(ModelEventManager{
        model_name, &iter->second.arch_events, &iter->second.fixed_events,
        &iter->second.model_events, &iter->second.misc_events});
  if (FXL_VLOG_IS_ON(4)) {
    model_event_manager->Dump();
  }
  return model_event_manager;
}

ModelEventManager::ModelEventManager(const std::string& model_name,
                                     const EventTable* arch_events,
                                     const EventTable* fixed_events,
                                     const EventTable* model_events,
                                     const EventTable* misc_events)
  : model_name_(model_name),
    arch_events_(arch_events),
    fixed_events_(fixed_events),
    model_events_(model_events),
    misc_events_(misc_events) {
}

bool ModelEventManager::EventIdToEventDetails(
    EventId id, const EventDetails** out_details) const {
  unsigned event = GetEventIdEvent(id);
  const EventTable* events;

  switch (GetEventIdGroup(id)) {
    case kGroupArch:
      events = arch_events_;
      break;
    case kGroupFixed:
      events = fixed_events_;
      break;
    case kGroupModel:
      events = model_events_;
      break;
    case kGroupMisc:
      events = misc_events_;
      break;
    default:
      return false;
  }

  if (event >= events->size()) {
    return false;
  }
  const EventDetails* details = (*events)[event];
  if (details->id == 0) {
    return false;
  }
  *out_details = details;
  return true;
}

// This just uses a linear search for now.
bool ModelEventManager::LookupEventByName(
    const char* group_name, const char* event_name,
    const EventDetails** out_details) const {
  const EventTable* events;

  if (strcmp(group_name, internal::kArchGroupName) == 0) {
    events = arch_events_;
  } else if (strcmp(group_name, internal::kFixedGroupName) == 0) {
    events = fixed_events_;
  } else if (strcmp(group_name, internal::kModelGroupName) == 0) {
    events = model_events_;
  } else if (strcmp(group_name, internal::kMiscGroupName) == 0) {
    events = misc_events_;
  } else {
    return false;
  }

  for (auto event : *events) {
    if (event->id == 0)
      continue;
    if (strcmp(event->name, event_name) == 0) {
      *out_details = event;
      return true;
    }
  }

  return false;
}

static void FillGroupTable(const char* name,
                           const ModelEventManager::EventTable* events,
                           ModelEventManager::GroupTable* groups) {
  groups->emplace_back(ModelEventManager::GroupEvents{name, {}});
  ModelEventManager::GroupEvents& group_events = groups->back();
  for (const auto& event : *events) {
    if (event->id != 0) {
      group_events.events.push_back(event);
    }
  }
}

ModelEventManager::GroupTable ModelEventManager::GetAllGroups() const {
  GroupTable groups;

  // Note: This makes copies of all the tables so that the result is not linked
  // to this object's lifetime. It also allows us to remove empty slots
  // (id == 0).
  FillGroupTable(internal::kArchGroupName, arch_events_, &groups);
  FillGroupTable(internal::kFixedGroupName, fixed_events_, &groups);
  FillGroupTable(internal::kModelGroupName, model_events_, &groups);
  FillGroupTable(internal::kMiscGroupName, misc_events_, &groups);

  return groups;
}

void ModelEventManager::Dump() const {
  printf("Dump of events for model %s\n", model_name_.c_str());
  DumpGroup(internal::kArchGroupName, arch_events_);
  DumpGroup(internal::kFixedGroupName, fixed_events_);
  DumpGroup(internal::kModelGroupName, model_events_);
  DumpGroup(internal::kMiscGroupName, misc_events_);
}

void ModelEventManager::DumpGroup(const char* name,
                                  const EventTable* events) const {
  printf("Group %s\n", name);
  for (const auto& event : *events) {
    if (event->id != 0) {
      printf("  %s\n", event->name);
    }
  }
}

}  // namespace perfmon
