// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/async-loop/cpp/loop.h"
#include "lib/component/cpp/exposed_object.h"
#include "lib/component/cpp/startup_context.h"

const char* VALUE = "value";

class Item : public component::ExposedObject {
 public:
  Item() : ExposedObject(UniqueName("item-")) {
    object_dir().set_metric(VALUE, component::IntMetric(0));
  }

  uint64_t size() { return object_dir().name().size() + 8; }

  void add_value(int64_t value) { object_dir().add_metric(VALUE, value); }
};

class Table : public component::ExposedObject {
 public:
  Table(const std::string& name) : ExposedObject("table-" + name) {
    object_dir().set_metric(
        {"item_size"},
        component::CallbackMetric([this](component::Metric* out_metric) {
          uint64_t sum = 0;
          for (const auto& item : items_) {
            sum += item->size();
          }
          out_metric->SetUInt(sum);
        }));
    object_dir().set_prop("version", "1.0");
    // Try binary values and keys.
    object_dir().set_prop("frame", std::vector<uint8_t>({0x10, 0x00, 0x10}));
    object_dir().set_prop(std::string("\x10\x10", 2),
                          std::vector<uint8_t>({0, 0, 0}));
    object_dir().set_metric(std::string("\x10", 1), component::IntMetric(-10));
  }

  std::shared_ptr<Item> NewItem(int64_t value) {
    auto ret = std::make_shared<Item>();
    items_.emplace_back(ret);
    ret->add_value(value);
    add_child(ret.get());
    return ret;
  }

 private:
  std::vector<std::shared_ptr<Item>> items_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();

  Table t1("t1"), t2("t2");
  t1.set_parent(context->outgoing().object_dir());
  t2.set_parent(context->outgoing().object_dir());

  t1.NewItem(10);
  t1.NewItem(100);

  t2.NewItem(4);

  loop.Run();
  return 0;
}
