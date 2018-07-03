// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/context/cpp/formatting.h>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/vector.h>

namespace fuchsia {
namespace modular {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  os << "[ ";
  for (const T& element : *value) {
    os << element << " ";
  }
  return os << "]";
}

std::ostream& operator<<(std::ostream& os, const FocusedState& state) {
  return os << (state.state == FocusedStateState::FOCUSED ? "yes" : "no");
}

std::ostream& operator<<(std::ostream& os, const StoryMetadata& meta) {
  return os << "{ id: " << meta.id << ", focused: " << *meta.focused << " }";
}

std::ostream& operator<<(std::ostream& os, const ModuleMetadata& meta) {
  return os << "{ url: " << meta.url << ", path: " << meta.path << " }";
}

std::ostream& operator<<(std::ostream& os, const EntityMetadata& meta) {
  return os << "{ topic: " << meta.topic << ", type: " << meta.type << " }";
}

std::ostream& operator<<(std::ostream& os, const LinkMetadata& meta) {
  return os << "{ module_path: " << meta.module_path << ", name: " << meta.name
            << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextMetadata& meta) {
  os << "{" << std::endl;
  os << "  story: " << meta.story << std::endl;
  os << "  link: " << meta.link << std::endl;
  os << "  mod: " << meta.mod << std::endl;
  os << "  entity: " << meta.entity << std::endl;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const ContextValue& value) {
  return os << "{ type: " << value.type << ", content: " << value.content
            << ", meta: " << value.meta << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextSelector& selector) {
  return os << "{ type: " << selector.type << ", meta: " << *selector.meta
            << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextUpdate& update) {
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

std::ostream& operator<<(std::ostream& os, const ContextQuery& query) {
  os << "{" << std::endl;
  for (auto it = query.selector->begin(); it != query.selector->end(); ++it) {
    os << "  " << (*it).key << ": " << (*it).value;
  }
  os << "}";
  return os;
}

}  // namespace modular
}  // namespace fuchsia
