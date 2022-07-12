// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/compatibility/hlcpp_client_app.h"

namespace fidl {
namespace test {
namespace compatibility {

EchoClientApp::EchoClientApp()
    : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

EchoPtr& EchoClientApp::echo() { return echo_; }

void EchoClientApp::Connect() {
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_.NewRequest());
}
}  // namespace compatibility
}  // namespace test
}  // namespace fidl
