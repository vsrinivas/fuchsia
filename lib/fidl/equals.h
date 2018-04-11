// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_EQUALS_H_
#define PERIDOT_LIB_FIDL_EQUALS_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

bool ChainDataEqual(const ChainData& a, const ChainData& b) {
  if (a.key_to_link_map->size() != b.key_to_link_map->size()) {
    return false;
  }

  std::map<fidl::StringPtr, const LinkPath*> a_map;
  for (const auto& i : *a.key_to_link_map) {
    a_map[i.key] = &i.link_path;
  }

  for (const auto& i : *b.key_to_link_map) {
    const auto& j = a_map.find(i.key);
    if (j == a_map.cend()) {
      // key not found
      return false;
    }
    if (*j->second != i.link_path) {
      // values don't match
      return false;
    }
  }

  return true;
}

bool SurfaceRelationEqual(const SurfaceRelation& a, const SurfaceRelation& b) {
  // TODO: what should we be doing for float comparison?
  return (a.arrangement == b.arrangement) && (a.dependency == b.dependency) &&
         (a.emphasis == b.emphasis);
}

bool SurfaceRelationEqual(const SurfaceRelationPtr& a,
                          const SurfaceRelationPtr& b) {
  if (a.get() == nullptr) {
    return (b.get() == nullptr);
  }
  if (b.get() == nullptr) {
    return false;
  }
  return SurfaceRelationEqual(*a, *b);
}

bool IntentParameterDataEqual(const IntentParameterData& a,
                              const IntentParameterData& b) {
  if (a.Which() != b.Which()) {
    return false;
  }

  switch (a.Which()) {
    case IntentParameterData::Tag::kEntityReference:
      return a.entity_reference() == b.entity_reference();
    case IntentParameterData::Tag::kJson:
      return a.json() == b.json();
    case IntentParameterData::Tag::kEntityType:
      // TODO(thatguy): should this be compared ignoring order?
      return a.entity_type() == b.entity_type();
    case IntentParameterData::Tag::kLinkName:
      return a.link_name() == b.link_name();
    case IntentParameterData::Tag::kLinkPath:
      return a.link_path() == b.link_path();
    case IntentParameterData::Tag::Invalid:
    default:
      return false;
  }
}

bool IntentEqual(const Intent& a, const Intent& b) {
  if ((a.action.handler != b.action.handler) ||
      (a.action.name != b.action.name) ||
      (a.parameters->size() != b.parameters->size())) {
    return false;
  }

  std::map<fidl::StringPtr, const IntentParameterData*> a_map;
  for (const auto& i : *a.parameters) {
    a_map[i.name] = &i.data;
  }

  for (const auto& i : *b.parameters) {
    const auto& j = a_map.find(i.name);
    if (j == a_map.cend()) {
      // name not found
      return false;
    }
    if (!IntentParameterDataEqual(*j->second, i.data)) {
      return false;
    }
  }

  return true;
}

bool IntentEqual(const IntentPtr& a, const IntentPtr& b) {
  if (a.get() == nullptr) {
    return (b.get() == nullptr);
  }
  if (b.get() == nullptr) {
    return false;
  }
  return IntentEqual(*a, *b);
}

bool ModuleDataEqual(const ModuleData& a, const ModuleData& b) {
  return a.module_url == b.module_url && a.module_path == b.module_path &&
         ChainDataEqual(a.chain_data, b.chain_data) &&
         a.link_path == b.link_path && a.module_source == b.module_source &&
         SurfaceRelationEqual(a.surface_relation, b.surface_relation) &&
         a.module_stopped == b.module_stopped &&
         IntentEqual(a.intent, b.intent);
}

bool ModuleDataEqual(const ModuleDataPtr& a, const ModuleDataPtr& b) {
  if (!a || !b)
    return !a && !b;
  return ModuleDataEqual(*a, *b);
}

}  // namespace modular

#endif
