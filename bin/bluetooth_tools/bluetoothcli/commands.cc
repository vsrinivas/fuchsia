// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <iostream>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/string_printf.h"

#include "app.h"
#include "helpers.h"
#include "logging.h"

namespace bluetoothcli {
namespace commands {
namespace {

bool HandleAvailable(const App* app,
                     const fxl::CommandLine& cmd_line,
                     const fxl::Closure& complete_cb) {
  app->control()->IsBluetoothAvailable([complete_cb](bool available) {
    CLI_LOG() << fxl::StringPrintf("Bluetooth is %savailable",
                                   available ? "" : "not ");
    complete_cb();
  });
  return true;
}

bool HandleListAdapters(const App* app,
                        const fxl::CommandLine& cmd_line,
                        const fxl::Closure& complete_cb) {
  app->control()->GetAdapters(
      [complete_cb](fidl::VectorPtr<bluetooth_control::AdapterInfo> adapters) {
        auto ac = fxl::MakeAutoCall(complete_cb);

        if (!adapters || adapters->size() == 0) {
          CLI_LOG() << "No adapters";
          return;
        }

        size_t i = 0;
        for (auto& adapter : *adapters) {
          CLI_LOG() << "Adapter " << i++ << ":";
          PrintAdapterInfo(adapter, 1);
        }
      });
  return true;
}

bool HandleActiveAdapter(const App* app,
                         const fxl::CommandLine& cmd_line,
                         const fxl::Closure& complete_cb) {
  app->control()->GetActiveAdapterInfo([complete_cb](auto adapter_info) {
    if (!adapter_info) {
      CLI_LOG() << "No active adapter";
      complete_cb();
      return;
    }
    PrintAdapterInfo(*adapter_info, 1);
    complete_cb();
  });

  return true;
}

bool HandleExit(App* app,
                const fxl::CommandLine& cmd_line,
                const fxl::Closure& complete_cb) {
  app->Quit();
  return true;
}

bool HandleStartDiscovery(App* app,
                          const fxl::CommandLine& cmd_line,
                          const fxl::Closure& complete_cb) {
  app->control()->RequestDiscovery(true, [complete_cb](auto status) {
    if (status.error) {
      CLI_LOG() << "Starting Discovery failed: " << status.error->description
                << ", (error = " << ErrorCodeToString(status.error->error_code)
                << ")";
    } else {
      CLI_LOG() << "Started discovery";
    }
    complete_cb();
  });

  return true;
}

bool HandleStopDiscovery(App* app,
                         const fxl::CommandLine& cmd_line,
                         const fxl::Closure& complete_cb) {
  app->control()->RequestDiscovery(false, [complete_cb](auto status) {
    if (status.error) {
      CLI_LOG() << "Stopping Discovery failed: " << status.error->description
                << ", (error = " << ErrorCodeToString(status.error->error_code)
                << ")";
    } else {
      CLI_LOG() << "Stopped discovery";
    }
    complete_cb();
  });

  return true;
}

bool HandleListDevices(const App* app,
                       const fxl::CommandLine& cmd_line,
                       const fxl::Closure& complete_cb) {
  if (app->discovered_devices().empty()) {
    CLI_LOG() << "No devices discovered";
    return false;
  }

  for (const auto& iter : app->discovered_devices()) {
    CLI_LOG() << "Device:";
    PrintRemoteDevice(iter.second, 1);
  }

  return false;
}

}  // namespace

void RegisterCommands(App* app,
                      bluetooth_tools::CommandDispatcher* dispatcher) {
  FXL_DCHECK(dispatcher);

#define BIND(handler) \
  std::bind(&handler, app, std::placeholders::_1, std::placeholders::_2)

  dispatcher->RegisterHandler("exit", "Exit the tool", BIND(HandleExit));
  dispatcher->RegisterHandler(
      "available", "Check if Bluetooth is available on this platform",
      BIND(HandleAvailable));
  dispatcher->RegisterHandler(
      "list-adapters", "Print information about available Bluetooth adapters",
      BIND(HandleListAdapters));
  dispatcher->RegisterHandler(
      "active-adapter", "Print information about the current active adapter",
      BIND(HandleActiveAdapter));
  dispatcher->RegisterHandler("start-discovery",
                              "Discover nearby Bluetooth devices",
                              BIND(HandleStartDiscovery));
  dispatcher->RegisterHandler("stop-discovery", "End device discovery",
                              BIND(HandleStopDiscovery));
  dispatcher->RegisterHandler("list-devices",
                              "List discovered Bluetooth devices",
                              BIND(HandleListDevices));

#undef BIND_HANDLER
}

}  // namespace commands
}  // namespace bluetoothcli
