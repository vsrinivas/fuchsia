// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server_app.h"

namespace echo {

EchoServer::EchoServer(bool quiet) : quiet_(quiet) {}

void EchoServer::EchoString(fidl::StringPtr value, EchoStringCallback callback) {
  if (!quiet_) {
    printf("EchoString: %s\n", value->data());
  }
  callback(std::move(value));
}

EchoServerApp::EchoServerApp(bool quiet)
    : EchoServerApp(sys::ComponentContext::CreateAndServeOutgoingDirectory(), quiet) {}

EchoServerApp::EchoServerApp(std::unique_ptr<sys::ComponentContext> context, bool quiet)
    : service_(new EchoServer(quiet)), context_(std::move(context)) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(service_.get()));
}

}  // namespace echo
