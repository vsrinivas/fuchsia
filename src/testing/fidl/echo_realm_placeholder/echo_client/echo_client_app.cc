// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_client_app.h"

namespace echo {

EchoClientApp::EchoClientApp()
    : EchoClientApp(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

EchoClientApp::EchoClientApp(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

void EchoClientApp::Start() { context_->svc()->Connect(echo_.NewRequest()); }

}  // namespace echo
