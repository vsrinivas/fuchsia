// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

/// This component implements echo server so that test can communicate with it and appmgr can also
/// publish out dir for this component.
int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  EchoImpl echo;
  context->outgoing()->AddPublicService(echo.GetHandler(loop.dispatcher()));
  loop.Run();
}
