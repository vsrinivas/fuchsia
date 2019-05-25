// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_COMPONENT_H_
#define LIB_INSPECT_COMPONENT_H_

#include <lib/inspect/health/health.h>
#include <lib/inspect/inspect.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

namespace inspect {

// ComponentInspector wraps an Inspector and Tree for a component.
// These objects are available globally so long as the ComponentInspector
// returned by Initialize is still alive.
class ComponentInspector {
 public:
  // Get the inspector for this component.
  Inspector* inspector() { return &inspector_; }

  // Get the root tree for this component.
  Tree* root_tree() { return &root_tree_; }

  // Initialize Inspection for the component. The returned ComponentInspector
  // must remain alive as long as inspection information needs to be available.
  [[nodiscard]] static std::shared_ptr<ComponentInspector> Initialize(
      sys::ComponentContext* startup_context);

  // Gets the singleton ComponentInspector for this process, if it exists.
  static std::shared_ptr<ComponentInspector> Get() { return singleton_.lock(); }

  // Gets the NodeHealth for this process.
  // This method is not thread safe.
  NodeHealth& Health();

 private:
  ComponentInspector();

  static std::weak_ptr<ComponentInspector> singleton_;

  std::unique_ptr<NodeHealth> component_health_;

  Inspector inspector_;
  Tree root_tree_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_COMPONENT_H_
