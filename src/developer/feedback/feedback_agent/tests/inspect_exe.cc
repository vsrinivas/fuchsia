// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect_deprecated/component.h>
#include <lib/sys/cpp/component_context.h>
#include <stdint.h>

#include <fs/vmo-file.h>

class Object {
 public:
  Object(inspect_deprecated::Node node, uint64_t value) : node_(std::move(node)) {
    version_ = node_.CreateStringProperty("version", "1.0");
    metric_ = node_.CreateIntMetric("value", value);
  }

 private:
  inspect_deprecated::Node node_;
  inspect_deprecated::StringProperty version_;
  inspect_deprecated::IntMetric metric_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = inspect_deprecated::ComponentInspector::Initialize(context.get());
  auto& root = inspector->root_tree()->GetRoot();

  Object o1(root.CreateChild("obj1"), 100);
  Object o2(root.CreateChild("obj2"), 200);

  loop.Run();
  return 0;
}
