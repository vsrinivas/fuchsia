// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_EXAMPLES_TODO_CPP_TODO_H_
#define APPS_MODULAR_EXAMPLES_TODO_CPP_TODO_H_

#include <random>

#include "lib/app/cpp/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "peridot/examples/todo_cpp/generator.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace todo {

using Key = fidl::Array<uint8_t>;

class TodoApp : public modular::Module, public ledger::PageWatcher,
                modular::Lifecycle {
 public:
  TodoApp();

  // modular::Module:
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override;

  // ledger::PageWatcher:
  void OnChange(ledger::PageChangePtr page_change,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

 private:
  // |modular.Lifecycle|
  void Terminate() override;

  void List(ledger::PageSnapshotPtr snapshot);

  void GetKeys(std::function<void(fidl::Array<Key>)> callback);

  void AddNew();

  void DeleteOne(fidl::Array<Key> keys);

  void Act();

  std::default_random_engine rng_;
  std::normal_distribution<> size_distribution_;
  std::uniform_int_distribution<> delay_distribution_;
  Generator generator_;
  std::unique_ptr<app::ApplicationContext> context_;
  fidl::Binding<modular::Module> module_binding_;
  fidl::InterfacePtr<modular::ModuleContext> module_context_;
  modular::ComponentContextPtr component_context_;
  ledger::LedgerPtr ledger_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  ledger::PagePtr page_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TodoApp);
};

}  // namespace todo

#endif  // APPS_MODULAR_EXAMPLES_TODO_CPP_TODO_H_
