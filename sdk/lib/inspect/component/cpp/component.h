// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_COMPONENT_CPP_COMPONENT_H_
#define LIB_INSPECT_COMPONENT_CPP_COMPONENT_H_

#include <lib/async/cpp/executor.h>
#include <lib/inspect/component/cpp/tree_handler_settings.h>
#include <lib/inspect/cpp/health.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

namespace inspect {
class ComponentInspector final {
 public:
  // Construct a ComponentInspector. Note that the OutgoingDirectory is not served automatically.
  explicit ComponentInspector(component::OutgoingDirectory& out, async_dispatcher_t* dispatcher,
                              Inspector inspector = {}, TreeHandlerSettings settings = {});

  ComponentInspector(ComponentInspector&&) = default;

  // Get the Inspector's root node.
  Node& root() { return inspector_.GetRoot(); }

  // Get the wrapped Inspector.
  Inspector* inspector() { return &inspector_; }

  // Gets the NodeHealth for this component.
  // This method is not thread safe.
  NodeHealth& Health();

  // Emplace a value in the wrapped Inspector.
  template <typename T>
  void emplace(T value) {
    inspector_.emplace(std::move(value));
  }

 private:
  ComponentInspector() = delete;
  ComponentInspector(const ComponentInspector&) = delete;

  Inspector inspector_;
  std::unique_ptr<NodeHealth> component_health_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_COMPONENT_CPP_COMPONENT_H_
