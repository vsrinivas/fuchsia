// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_EXAMPLES_TODO_CPP_TODO_H_
#define PERIDOT_EXAMPLES_TODO_CPP_TODO_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>

#include <random>

#include "peridot/examples/todo_cpp/generator.h"

namespace todo {

using Key = std::vector<uint8_t>;

class TodoApp : public fuchsia::ledger::PageWatcher,
                fuchsia::modular::Lifecycle {
 public:
  TodoApp(async::Loop* loop);

  // fuchsia::ledger::PageWatcher:
  void OnChange(fuchsia::ledger::PageChange page_change,
                fuchsia::ledger::ResultState result_state,
                OnChangeCallback callback) override;

 private:
  // |modular.fuchsia::modular::Lifecycle|
  void Terminate() override;

  void List(fuchsia::ledger::PageSnapshotPtr snapshot);

  void GetKeys(fit::function<void(std::vector<Key>)> callback);

  void AddNew();

  void DeleteOne(std::vector<Key> keys);

  void Act();

  async::Loop* const loop_;
  std::default_random_engine rng_;
  std::normal_distribution<> size_distribution_;
  std::uniform_int_distribution<> delay_distribution_;
  Generator generator_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::InterfacePtr<fuchsia::modular::ModuleContext> module_context_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::ledger::LedgerPtr ledger_;
  fidl::Binding<fuchsia::ledger::PageWatcher> page_watcher_binding_;
  fuchsia::ledger::PagePtr page_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TodoApp);
};

}  // namespace todo

#endif  // PERIDOT_EXAMPLES_TODO_CPP_TODO_H_
