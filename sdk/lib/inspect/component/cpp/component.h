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
// ComponentInspector is a component-wide instance of an Inspector that
// serves its Inspect data via the fuchsia.inspect.Tree protocol.
//
// Example:
//
// ```
// #include <lib/async-loop/cpp/loop.h>
// #include <lib/async-loop/default.h>
// #include <lib/inspect/component/cpp/component.h>
// #include "lib/sys/component/cpp/outgoing_directory.h"
//
// int main() {
//   using inspect::ComponentInspector;
//
//   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
//   auto* dispatcher = loop.dispatcher();
//   auto out = component::OutgoingDirectory::Create(dispatcher);
//   auto inspector = ComponentInspector(out, dispatcher);
//
//   inspector.root().RecordInt("val1", 1);
//
//   if (out.ServeFromStartupInfo().is_error()) {
//     return -1;
//   }
//
//   inspector.Health().Ok();
//
//   loop.Run();
//   return 0;
// }
// ```
class ComponentInspector final {
 public:
  // Construct a ComponentInspector a component-wide Inspector and host it on the given
  // outgoing directory.
  //
  // Note that it is the caller's responsibility to ensure the outgoing directory is served.
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

 private:
  ComponentInspector() = delete;
  ComponentInspector(const ComponentInspector&) = delete;

  Inspector inspector_;
  std::unique_ptr<NodeHealth> component_health_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_COMPONENT_CPP_COMPONENT_H_
