// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <cstdint>
#include <cstdlib>

#include "src/lib/fxl/logging.h"

class Object {
 public:
  Object(inspect::Node node, uint64_t value) : node_(std::move(node)) {
    version_ = node_.CreateString("version", "1.0");
    value_ = node_.CreateInt("value", value);
  }

 private:
  inspect::Node node_;
  inspect::StringProperty version_;
  inspect::IntProperty value_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  auto& root = inspector->root();

  Object o1(root.CreateChild("obj1"), 100);
  Object o2(root.CreateChild("obj2"), 200);

  loop.Run();

  return EXIT_SUCCESS;
}
