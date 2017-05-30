// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <iostream>

#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

#include "app.h"
#include "helpers.h"
#include "logging.h"

namespace bluetoothcli {
namespace commands {
namespace {

bool HandleAvailable(const App* app, const ftl::CommandLine& cmd_line,
                     const ftl::Closure& complete_cb) {
  app->adapter_manager()->IsBluetoothAvailable([complete_cb](bool available) {
    CLI_LOG() << ftl::StringPrintf("Bluetooth is %savailable", available ? "" : "not ");
    complete_cb();
  });
  return true;
}

bool HandleListAdapters(const App* app, const ftl::CommandLine& cmd_line,
                        const ftl::Closure& complete_cb) {
  app->adapter_manager()->GetAdapters(
      [complete_cb](fidl::Array<bluetooth::control::AdapterInfoPtr> adapters) {
        auto ac = ftl::MakeAutoCall(complete_cb);

        if (!adapters || adapters.size() == 0) {
          CLI_LOG() << "No adapters";
          return;
        }

        size_t i = 0;
        for (auto& adapter : adapters) {
          CLI_LOG() << "Adapter " << i++ << ":";
          PrintAdapterInfo(adapter, 1);
        }
      });
  return true;
}

bool HandleActiveAdapter(const App* app, const ftl::CommandLine& cmd_line,
                         const ftl::Closure& complete_cb) {
  if (!app->active_adapter()) {
    CLI_LOG() << "No active adapter";
    return false;
  }

  app->active_adapter()->GetInfo([complete_cb](bluetooth::control::AdapterInfoPtr adapter_info) {
    PrintAdapterInfo(adapter_info);
    complete_cb();
  });

  return true;
}

bool HandleExit(const App* app, const ftl::CommandLine& cmd_line, const ftl::Closure& complete_cb) {
  mtl::MessageLoop::GetCurrent()->QuitNow();
  return true;
}

}  // namespace

void RegisterCommands(App* app, bluetooth::tools::CommandDispatcher* dispatcher) {
  FTL_DCHECK(dispatcher);

#define BIND(handler) std::bind(&handler, app, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("exit", "Exit the tool", BIND(HandleExit));
  dispatcher->RegisterHandler("available", "Check if Bluetooth is available on this platform",
                              BIND(HandleAvailable));
  dispatcher->RegisterHandler("list-adapters",
                              "Print information about available Bluetooth adapters",
                              BIND(HandleListAdapters));
  dispatcher->RegisterHandler("active-adapter",
                              "Print information about the current active adapter",
                              BIND(HandleActiveAdapter));

#undef BIND_HANDLER
}

}  // namespace commands
}  // namespace bluetoothcli
