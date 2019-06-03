// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

int main(int argc, const char** argv) {
  // kAsyncLoopConfigAttachToThread is currently required by
  // component::Outgoing() which can currently only construct using
  // async_get_default_dispatcher().
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  App app;
  loop.Run();
  return 0;
}
