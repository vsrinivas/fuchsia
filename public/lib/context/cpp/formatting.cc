// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/metadata.fidl.h"

namespace maxwell {

std::ostream& operator<<(std::ostream& os, const FocusedState& state) {
  return os << (state.state == FocusedState::State::FOCUSED ? "yes" : "no");
}

std::ostream& operator<<(std::ostream& os, const StoryMetadata& meta) {
  return os << "{ id: " << meta.id << ", focused: " << meta.focused << " }";
}

std::ostream& operator<<(std::ostream& os, const ModuleMetadata& meta) {
  return os << "{ url: " << meta.url << ", path: " << meta.path << " }";
}

std::ostream& operator<<(std::ostream& os, const EntityMetadata& meta) {
  return os << "{ topic: " << meta.topic << ", type: " << meta.type << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextMetadata& meta) {
  os << "{" << std::endl;
  os << "  story: " << meta.story << std::endl;
  os << "  mod: " << meta.mod << std::endl;
  os << "  entity: " << meta.entity << std::endl;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const ContextValue& value) {
  return os << "{ type: " << value.type << ", content: " << value.content
            << ", meta: " << value.meta << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextSelector& selector) {
  return os << "{ type: " << selector.type << ", meta: " << selector.meta
            << " }";
}

std::ostream& operator<<(std::ostream& os, const ContextUpdate& update) {
  os << "{" << std::endl;
  for (auto it = update.values.cbegin(); it != update.values.cend(); ++it) {
    os << "  " << it.GetKey() << ":" << std::endl;
    int i = 0;
    for (const auto& v : it.GetValue()) {
      os << "    [" << i++ << "]: " << v;
    }
  }
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const ContextQuery& query) {
  os << "{" << std::endl;
  for (auto it = query.selector.cbegin(); it != query.selector.cend(); ++it) {
    os << "  " << it.GetKey() << ": " << it.GetValue();
  }
  os << "}";
  return os;
}

}  // namespace maxwell
