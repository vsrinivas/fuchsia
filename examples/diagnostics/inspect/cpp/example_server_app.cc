// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "example_server_app.h"

#include <lib/sys/cpp/component_context.h>

#include "lib/fidl/cpp/interface_request.h"

namespace example {

constexpr size_t kRequestHistogramBuckets = 10;
constexpr uint64_t kRequestHistogramFloor = 1;
constexpr uint64_t kRequestHistogramInitialStep = 1;
constexpr uint64_t kRequestHistogramStepMultiplier = 2;

ExampleServerApp::ExampleServerApp()
    : ExampleServerApp(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

ExampleServerApp::ExampleServerApp(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {
  inspector_ = std::make_unique<sys::ComponentInspector>(context_.get());
  connections_node_ = inspector_->root().CreateChild("connections");

  echo_stats_ = std::make_shared<EchoConnectionStats>(EchoConnectionStats{
      inspector_->root().CreateExponentialUintHistogram(
          "request_size_histogram", kRequestHistogramFloor, kRequestHistogramInitialStep,
          kRequestHistogramStepMultiplier, kRequestHistogramBuckets),
      inspector_->root().CreateUint("total_requests", 0),
  });

  context_->outgoing()->AddPublicService<EchoConnection::Echo>(
      [this](fidl::InterfaceRequest<EchoConnection::Echo> request) {
        bindings_.AddBinding(
            std::make_unique<EchoConnection>(
                connections_node_.CreateChild(std::to_string(connection_count_++)), echo_stats_),
            std::move(request));
      });
}

}  // namespace example
