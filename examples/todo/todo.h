// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_EXAMPLES_TODO_TODO_H_
#define APPS_LEDGER_EXAMPLES_TODO_TODO_H_

#include <random>

#include "apps/ledger/examples/todo/generator.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace todo {

using Key = fidl::Array<uint8_t>;

class TodoApp : public ledger::PageWatcher {
 public:
  TodoApp(ftl::CommandLine command_line);

  // ledger::PageWatcher:
  void OnInitialState(fidl::InterfaceHandle<ledger::PageSnapshot> snapshot,
                      const OnInitialStateCallback& callback) override;
  void OnChange(ledger::PageChangePtr page_change,
                const OnChangeCallback& callback) override;

 private:
  ledger::LedgerPtr GetLedger();

  void List();

  void GetKeys(std::function<void(fidl::Array<Key>)> callback);

  void AddNew();

  void DeleteOne(fidl::Array<Key> keys);

  void Act();

  std::default_random_engine rng_;
  std::normal_distribution<> size_distribution_;
  std::uniform_int_distribution<> delay_distribution_;
  ftl::CommandLine command_line_;
  Generator generator_;
  std::unique_ptr<modular::ApplicationContext> context_;
  modular::ApplicationControllerPtr ledger_controller_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  ledger::PagePtr page_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TodoApp);
};

}  // namespace todo

#endif  // APPS_LEDGER_EXAMPLES_TODO_TODO_H_
