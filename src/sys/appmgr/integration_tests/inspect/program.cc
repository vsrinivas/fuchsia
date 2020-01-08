// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/inspect_deprecated/deprecated/exposed_object.h"
#include "src/lib/inspect_deprecated/deprecated/object_dir.h"

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
    object_dir().set_metric({"item_size"},
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
    object_dir().set_prop(std::string("\x10\x10", 2), std::vector<uint8_t>({0, 0, 0}));
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
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  Table t1("t1"), t2("t2");
  auto root_object = component::ObjectDir::Make("root");
  t1.set_parent(root_object);
  t2.set_parent(root_object);

  t1.NewItem(10);
  t1.NewItem(100);

  t2.NewItem(4);

  // It is not an error to use an invalid ObjectDir, but it will not have an
  // effect.
  component::ObjectDir invalid;
  invalid.find({"test", "a"});
  invalid.set_prop("test1", "...");
  invalid.set_metric("test2", component::IntMetric(10));
  invalid.set_child(component::Object::Make("temp"));
  invalid.set_children_callback([](std::vector<std::shared_ptr<component::Object>>* out) {});
  invalid.add_metric("test2", 2);
  invalid.sub_metric("test2", 2);

  // Check that setting and moving parents works correctly.
  Table subtable("subtable");
  subtable.set_parent(t1.object_dir());
  subtable.NewItem(10)->add_value(10);
  subtable.set_parent(t2.object_dir());

  // Remove a child to unlink it from its parent.
  Table subtable2("subtable2");
  subtable2.set_parent(t1.object_dir());
  subtable2.remove_from_parent();
  subtable2.remove_from_parent();  // Repeated remove has no effect.

  // Set parent to invalid, which will unlink from parent.
  Table subtable3("subtable3");
  subtable2.set_parent(t1.object_dir());
  subtable2.set_parent(invalid);
  root_object.set_children_callback([](component::Object::ObjectVector* out) {
    auto dir = component::ObjectDir::Make("lazy_child");
    dir.set_prop("version", "1");
    out->push_back(dir.object());
  });
  fidl::BindingSet<fuchsia::inspect::deprecated::Inspect> inspect_bindings_;
  context->outgoing()
      ->GetOrCreateDirectory("diagnostics")
      ->AddEntry(
          fuchsia::inspect::deprecated::Inspect::Name_,
          std::make_unique<vfs::Service>(inspect_bindings_.GetHandler(root_object.object().get())));

  loop.Run();
  return 0;
}
