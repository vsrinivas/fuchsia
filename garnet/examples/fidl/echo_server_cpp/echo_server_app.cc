// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server_app.h"

#include "lib/sys/cpp/component_context.h"

namespace echo {

EchoServerApp::EchoServerApp(bool quiet)
    : EchoServerApp(sys::ComponentContext::Create(), quiet) {}

EchoServerApp::EchoServerApp(std::unique_ptr<sys::ComponentContext> context,
                             bool quiet)
    : context_(std::move(context)), quiet_(quiet) {
  context_->outgoing2()->AddPublicService(bindings_.GetHandler(this));
}

void EchoServerApp::EchoString(fidl::StringPtr value,
                               EchoStringCallback callback) {
  if (!quiet_) {
    printf("EchoString: %s\n", value->data());
  }
  callback(std::move(value));
}

}  // namespace echo
