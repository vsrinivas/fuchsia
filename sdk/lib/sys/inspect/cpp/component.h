// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_INSPECT_CPP_COMPONENT_H_
#define LIB_SYS_INSPECT_CPP_COMPONENT_H_

#include <lib/inspect/cpp/health.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>

namespace sys {

// ComponentInspector is a singleton wrapping an Inspector for a Fuchsia Component.
//
// Callers must ensure the ComponentInspector returned by Initialize remains alive for the lifetime
// of the component.
class ComponentInspector final {
 public:
  // Creates a new Inspector for this component, and publishes it in this component's outgoing
  // directory at the path "inspect/root.inspect".
  explicit ComponentInspector(sys::ComponentContext* startup_context);

  // Get the inspector for this component.
  ::inspect::Inspector* inspector() { return &inspector_; }

  // Get the root tree for this component.
  ::inspect::Node& root() { return inspector_.GetRoot(); }

  // Gets the NodeHealth for this component.
  // This method is NOT thread safe.
  ::inspect::NodeHealth& Health();

  // Emplace a value in the wrapped Inspector.
  template <typename T>
  void emplace(T value) {
    inspector_.emplace(std::move(value));
  }

 private:
  ComponentInspector();

  std::unique_ptr<::inspect::NodeHealth> component_health_;

  ::inspect::Inspector inspector_;
};

}  // namespace sys

#endif  // LIB_SYS_INSPECT_CPP_COMPONENT_H_
