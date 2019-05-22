// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_INSPECTION_EXAMPLE_SERVER_APP_H_
#define GARNET_EXAMPLES_INSPECTION_EXAMPLE_SERVER_APP_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/component.h>
#include <lib/sys/cpp/component_context.h>

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
  std::shared_ptr<inspect::ComponentInspector> inspector_;
  inspect::Node connections_node_;
  size_t connection_count_ = 0;
  std::shared_ptr<EchoConnectionStats> echo_stats_;
  fidl::BindingSet<EchoConnection::Echo, std::unique_ptr<EchoConnection>>
      bindings_;
};

}  // namespace example

#endif  // GARNET_EXAMPLES_INSPECTION_EXAMPLE_SERVER_APP_H_
