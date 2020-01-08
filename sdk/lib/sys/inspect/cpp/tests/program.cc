// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());

  inspector->root().CreateInt("val1", 1, inspector.get());
  inspector->root().CreateInt("val2", 2, inspector.get());
  inspector->root().CreateInt("val3", 3, inspector.get());

  inspector->Health().Ok();

  loop.Run();
  return 0;
}
