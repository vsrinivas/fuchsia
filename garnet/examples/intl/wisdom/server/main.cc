// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "intl_wisdom_server_impl.h"

#include "lib/async-loop/cpp/loop.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  intl_wisdom::IntlWisdomServerImpl server(sys::ComponentContext::Create());
  loop.Run();
  return 0;
}
