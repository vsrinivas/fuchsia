// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());

  inspector->root().CreateString("version", "v1", inspector.get());
  auto option_a = inspector->root().CreateChild("option_a");
  option_a.CreateInt("value", 10, inspector.get());

  auto option_b = inspector->root().CreateChild("option_b");
  option_b.CreateInt("value", 20, inspector.get());

  loop.Run();
  return 0;
}
