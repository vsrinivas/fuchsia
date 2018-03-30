// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/formatting.h"

#include <fuchsia/cpp/modular.h>

#include "lib/fidl/cpp/vector.h"

namespace modular {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  os << "[ ";
  for (const T& element : *value) {
    os << element << " ";
  }
  return os << "]";
}

std::ostream& operator<<(std::ostream& os, const modular::FocusedState& state) {
  return os << (state.state == modular::FocusedStateState::FOCUSED ? "yes" : "no");
}

std::ostream& operator<<(std::ostream& os, const modular::StoryMetadata& meta) {
  return os << "{ id: " << meta.id << ", focused: " << *meta.focused << " }";
}

std::ostream& operator<<(std::ostream& os,
                         const modular::ModuleMetadata& meta) {
  return os << "{ url: " << meta.url << ", path: " << meta.path << " }";
}

std::ostream& operator<<(std::ostream& os,
                         const modular::EntityMetadata& meta) {
  return os << "{ topic: " << meta.topic << ", type: " << meta.type << " }";
}

std::ostream& operator<<(std::ostream& os, const modular::LinkMetadata& meta) {
  return os << "{ module_path: " << meta.module_path << ", name: " << meta.name
            << " }";
}

std::ostream& operator<<(std::ostream& os,
                         const modular::ContextMetadata& meta) {
  os << "{" << std::endl;
  os << "  story: " << *meta.story << std::endl;
  os << "  link: " << *meta.link << std::endl;
  os << "  mod: " << *meta.mod << std::endl;
  os << "  entity: " << *meta.entity << std::endl;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const modular::ContextValue& value) {
  return os << "{ type: " << value.type << ", content: " << value.content
            << ", meta: " << value.meta << " }";
}

std::ostream& operator<<(std::ostream& os,
                         const modular::ContextSelector& selector) {
  return os << "{ type: " << selector.type << ", meta: " << *selector.meta
            << " }";
}

std::ostream& operator<<(std::ostream& os,
                         const modular::ContextUpdate& update) {
  os << "{" << std::endl;
  for (auto it = update.values->begin(); it != update.values->end(); ++it) {
    os << "  " << (*it).key << ":" << std::endl;
    int i = 0;
    for (const auto& v : *(*it).value) {
      os << "    [" << i++ << "]: " << v;
    }
  }
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const modular::ContextQuery& query) {
  os << "{" << std::endl;
  for (auto it = query.selector->begin(); it != query.selector->end(); ++it) {
    os << "  " << (*it).key << ": " << (*it).value;
  }
  os << "}";
  return os;
}

}  // namespace modular
