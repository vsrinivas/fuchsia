// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "intl_wisdom_server_impl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/default.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  intl_wisdom::IntlWisdomServerImpl server(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());
  loop.Run();
  return 0;
}
