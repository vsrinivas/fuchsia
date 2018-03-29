// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_EQUALS_H_
#define PERIDOT_LIB_FIDL_EQUALS_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

bool StringVectorEqual(const fidl::VectorPtr<fidl::StringPtr>& a,
                       const fidl::VectorPtr<fidl::StringPtr>& b) {
  return std::equal(a->begin(), a->end(), b->begin(), b->end());
}

bool LinkPathEqual(const LinkPath& a, const LinkPath& b) {
  return (a.link_name == b.link_name) &&
         StringVectorEqual(a.module_path, b.module_path);
}

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
    if (!LinkPathEqual(*j->second, i.link_path)) {
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

bool NounEqual(const Noun& a, const Noun& b) {
  if (a.Which() != b.Which()) {
    return false;
  }

  switch (a.Which()) {
    case Noun::Tag::kEntityReference:
      return a.entity_reference() == b.entity_reference();
    case Noun::Tag::kJson:
      return a.json() == b.json();
    case Noun::Tag::kEntityType:
      // TODO(thatguy): should this be compared ignoring order?
      return StringVectorEqual(a.entity_type(), b.entity_type());
    case Noun::Tag::kLinkName:
      return a.link_name() == b.link_name();
    case Noun::Tag::kLinkPath:
      return LinkPathEqual(a.link_path(), b.link_path());
    case Noun::Tag::Invalid:
    default:
      return false;
  }
}

bool DaisyEqual(const Daisy& a, const Daisy& b) {
  if ((a.url != b.url) || (a.verb != b.verb) ||
      (a.nouns->size() != b.nouns->size())) {
    return false;
  }

  std::map<fidl::StringPtr, const Noun*> a_map;
  for (const auto& i : *a.nouns) {
    a_map[i.name] = &i.noun;
  }

  for (const auto& i : *b.nouns) {
    const auto& j = a_map.find(i.name);
    if (j == a_map.cend()) {
      // name not found
      return false;
    }
    if (!NounEqual(*j->second, i.noun)) {
      return false;
    }
  }

  return true;
}

bool DaisyEqual(const DaisyPtr& a, const DaisyPtr& b) {
  if (a.get() == nullptr) {
    return (b.get() == nullptr);
  }
  if (b.get() == nullptr) {
    return false;
  }
  return DaisyEqual(*a, *b);
}

bool ModuleDataEqual(const ModuleData& a, const ModuleData& b) {
  return (a.module_url == b.module_url) &&
         StringVectorEqual(a.module_path, b.module_path) &&
         ChainDataEqual(a.chain_data, b.chain_data) &&
         LinkPathEqual(a.link_path, b.link_path) &&
         (a.module_source == b.module_source) &&
         SurfaceRelationEqual(a.surface_relation, b.surface_relation) &&
         (a.module_stopped == b.module_stopped) && DaisyEqual(a.daisy, b.daisy);
}

}  // namespace modular

#endif
