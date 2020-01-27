// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

class Item {
 public:
  explicit Item(inspect::Node node) : node_(std::move(node)) {
    value_ = node_.CreateInt("value", 0);
  }

  void Add(int64_t value) { value_.Add(value); }

 private:
  inspect::Node node_;
  inspect::IntProperty value_;
};

class Table {
 public:
  explicit Table(inspect::Node node) : node_(std::move(node)) {
    version_ = node_.CreateString("version", "1.0");
    frame_ = node_.CreateByteVector("frame", std::vector<uint8_t>({0, 0, 0}));
    metric_ = node_.CreateInt("value", -10);
    isActive_ = node_.CreateBool("active", true);
  }

  std::shared_ptr<Item> NewItem(int64_t value) {
    auto ret = std::make_shared<Item>(node_.CreateChild(node_.UniqueName("item-")));
    items_.emplace_back(ret);
    ret->Add(value);
    return ret;
  }

 private:
  inspect::Node node_;
  inspect::StringProperty version_;
  inspect::ByteVectorProperty frame_;
  inspect::IntProperty metric_;
  inspect::BoolProperty isActive_;
  std::vector<std::shared_ptr<Item>> items_;
};

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());

  Table t1(inspector->root().CreateChild("t1"));
  Table t2(inspector->root().CreateChild("t2"));

  t1.NewItem(10);
  t1.NewItem(90)->Add(10);

  t2.NewItem(2)->Add(2);

  loop.Run();
  return 0;
}
