// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/vmo-file.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/component.h>
#include <lib/sys/cpp/component_context.h>

class Item {
 public:
  Item(inspect::Object object) : object_(std::move(object)) {
    value_ = object_.CreateIntMetric("value", 0);
  }

  void Add(int64_t value) { value_.Add(value); }

 private:
  inspect::Object object_;
  inspect::IntMetric value_;
};

class Table {
 public:
  Table(inspect::Object object) : object_(std::move(object)) {
    version_ = object_.CreateStringProperty("version", "1.0");
    frame_ = object_.CreateByteVectorProperty("frame",
                                              std::vector<uint8_t>({0, 0, 0}));
    metric_ = object_.CreateIntMetric("value", -10);
  }

  std::shared_ptr<Item> NewItem(int64_t value) {
    auto ret = std::make_shared<Item>(
        object_.CreateChild(inspect::UniqueName("item-")));
    items_.emplace_back(ret);
    ret->Add(value);
    return ret;
  }

 private:
  inspect::Object object_;
  inspect::StringProperty version_;
  inspect::ByteVectorProperty frame_;
  inspect::IntMetric metric_;
  std::vector<std::shared_ptr<Item>> items_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = inspect::ComponentInspector::Initialize(context.get());
  auto& root = inspector->root_tree()->GetRoot();

  Table t1(root.CreateChild("t1"));
  Table t2(root.CreateChild("t2"));

  t1.NewItem(10);
  t1.NewItem(90)->Add(10);

  t2.NewItem(2)->Add(2);

  loop.Run();
  return 0;
}
