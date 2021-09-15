// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "example_server_app.h"

namespace example {

ExampleServerApp::ExampleServerApp()
    : ExampleServerApp(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

ExampleServerApp::ExampleServerApp(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {
  // [START initialization]
  inspector_ = std::make_unique<sys::ComponentInspector>(context_.get());
  // [END initialization]

  // [START properties]
  // Attach properties to the root node of the tree
  inspect::Node& root_node = inspector_->root();
  // Important: Hold references to properties and don't let them go out of scope.
  auto total_requests = root_node.CreateUint("total_requests", 0);
  auto bytes_processed = root_node.CreateUint("bytes_processed", 0);
  // [END properties]

  // [START health_check]
  inspector_->Health().StartingUp();

  // [START_EXCLUDE]
  echo_stats_ = std::make_shared<EchoConnectionStats>(EchoConnectionStats{
      std::move(bytes_processed),
      std::move(total_requests),
  });

  context_->outgoing()->AddPublicService<EchoConnection::Echo>(
      [this](fidl::InterfaceRequest<EchoConnection::Echo> request) {
        bindings_.AddBinding(std::make_unique<EchoConnection>(echo_stats_), std::move(request));
      });
  // [END_EXCLUDE]

  inspector_->Health().Ok();
  // [END health_check]
}

}  // namespace example
