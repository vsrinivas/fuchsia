// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/component.h>
#include <lib/sys/cpp/component_context.h>

#include "fs/vmo-file.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = inspect::ComponentInspector::Initialize(context.get());
  inspect::ComponentInspector::Get()->Health().Unhealthy("Example failure");

  loop.Run();
  return 0;
}
