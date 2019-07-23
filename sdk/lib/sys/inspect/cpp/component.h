// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_INSPECT_CPP_COMPONENT_H_
#define LIB_SYS_INSPECT_CPP_COMPONENT_H_

#include <lib/inspect/cpp/health.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

namespace sys {

// ComponentInspector wraps an Inspector and Tree for a component.
// These objects are available globally so long as the ComponentInspector
// returned by Initialize is still alive.
class ComponentInspector final {
 public:
  // Get the inspector for this component.
  ::inspect::Inspector* inspector() { return &inspector_; }

  // Get the root tree for this component.
  ::inspect::Node& root() { return inspector_.GetRoot(); }

  // Initialize Inspection for the component. The returned ComponentInspector
  // must remain alive as long as inspection information needs to be available.
  static std::shared_ptr<ComponentInspector> Initialize(sys::ComponentContext* startup_context);

  // Gets the singleton ComponentInspector for this process, if it exists.
  static std::shared_ptr<ComponentInspector> Get() { return singleton_.lock(); }

  // Gets the NodeHealth for this component.
  // This method is not thread safe.
  ::inspect::NodeHealth& Health();

 private:
  ComponentInspector();

  static std::weak_ptr<ComponentInspector> singleton_;

  std::unique_ptr<::inspect::NodeHealth> component_health_;

  ::inspect::Inspector inspector_;
};

}  // namespace sys

#endif  // LIB_SYS_INSPECT_CPP_COMPONENT_H_
