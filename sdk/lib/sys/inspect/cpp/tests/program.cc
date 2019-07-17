// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  std::vector<inspect::IntProperty> properties;
  auto context = sys::ComponentContext::Create();
  auto inspector = sys::ComponentInspector::Initialize(context.get());

  properties.emplace_back(inspector->root().CreateInt("val1", 1));
  properties.emplace_back(inspector->root().CreateInt("val2", 2));
  properties.emplace_back(sys::ComponentInspector::Get()->root().CreateInt("val3", 3));

  inspector->Health().Ok();

  loop.Run();
  return 0;
}
