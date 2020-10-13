// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CPP_EXAMPLE_SERVER_APP_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CPP_EXAMPLE_SERVER_APP_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include "echo_connection.h"

namespace example {

class ExampleServerApp {
 public:
  ExampleServerApp();

 protected:
  explicit ExampleServerApp(std::unique_ptr<sys::ComponentContext> context);

 private:
  ExampleServerApp(const ExampleServerApp&) = delete;
  ExampleServerApp& operator=(const ExampleServerApp&) = delete;

  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<sys::ComponentInspector> inspector_;
  inspect::Node connections_node_;
  size_t connection_count_ = 0;
  std::shared_ptr<EchoConnectionStats> echo_stats_;
  fidl::BindingSet<EchoConnection::Echo, std::unique_ptr<EchoConnection>> bindings_;
};

}  // namespace example

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CPP_EXAMPLE_SERVER_APP_H_
