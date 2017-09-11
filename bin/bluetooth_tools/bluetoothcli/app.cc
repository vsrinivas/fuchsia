// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <iostream>

#include <linenoise.h>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/mtl/tasks/message_loop.h"

#include "commands.h"
#include "helpers.h"
#include "logging.h"

namespace bluetoothcli {

App::App()
    : context_(app::ApplicationContext::CreateFromStartupInfo()),
      manager_delegate_(this),
      adapter_delegate_(this) {
  FXL_DCHECK(context_);

  adapter_manager_ = context_->ConnectToEnvironmentService<bluetooth::control::AdapterManager>();
  FXL_DCHECK(adapter_manager_);

  adapter_manager_.set_connection_error_handler([] {
    CLI_LOG() << "AdapterManager disconnected";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  commands::RegisterCommands(this, &command_dispatcher_);

  // Register with the AdapterManager as its delegate.
  bluetooth::control::AdapterManagerDelegatePtr delegate;
  fidl::InterfaceRequest<bluetooth::control::AdapterManagerDelegate> delegate_request =
      fidl::GetProxy(&delegate);
  manager_delegate_.Bind(std::move(delegate_request));

  adapter_manager_->SetDelegate(std::move(delegate));

  adapter_manager_->IsBluetoothAvailable([this](bool available) {
    if (!available) return;

    adapter_manager_->GetActiveAdapter(fidl::GetProxy(&active_adapter_));
    bluetooth::control::AdapterDelegatePtr delegate;
    auto request = fidl::GetProxy(&delegate);
    adapter_delegate_.Bind(std::move(request));
    active_adapter_->SetDelegate(std::move(delegate));
  });
}

void App::ReadNextInput() {
  bool call_complete_cb = true;
  auto complete_cb = [this] {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] { ReadNextInput(); });
  };

  char* line = linenoise("bluetooth> ");
  if (!line) {
    mtl::MessageLoop::GetCurrent()->QuitNow();
    return;
  }

  auto ac = fxl::MakeAutoCall([&call_complete_cb, line, complete_cb] {
    if (call_complete_cb) complete_cb();
    free(line);
  });

  auto split = fxl::SplitStringCopy(fxl::StringView(line, std::strlen(line)), " ",
                                    fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (split.empty() || split[0] == "help") {
    linenoiseHistoryAdd(line);
    command_dispatcher_.DescribeAllCommands();
    return;
  }

  bool cmd_found;
  if (!command_dispatcher_.ExecuteCommand(split, complete_cb, &cmd_found)) {
    if (!cmd_found) CLI_LOG() << "Unknown command: " << line;
    return;
  }

  call_complete_cb = false;
  linenoiseHistoryAdd(line);
}

void App::OnActiveAdapterChanged(bluetooth::control::AdapterInfoPtr active_adapter) {
  if (!active_adapter) {
    CLI_LOG() << "\n>>>> Active adapter is (null)";
    active_adapter_ = nullptr;
    return;
  }

  CLI_LOG() << "\n>>>> Active adapter: (id=" << active_adapter->identifier << ")\n";

  adapter_manager_->GetActiveAdapter(fidl::GetProxy(&active_adapter_));
  bluetooth::control::AdapterDelegatePtr delegate;
  fidl::InterfaceRequest<bluetooth::control::AdapterDelegate> request = fidl::GetProxy(&delegate);
  adapter_delegate_.Bind(std::move(request));
  active_adapter_->SetDelegate(std::move(delegate));
}

void App::OnAdapterAdded(bluetooth::control::AdapterInfoPtr adapter) {
  CLI_LOG() << "\n>>>> Adapter added (id=" << adapter->identifier << ")\n";
}

void App::OnAdapterRemoved(const ::fidl::String& identifier) {
  CLI_LOG() << "\n>>>> Adapter removed (id=" << identifier << ")\n";
}

void App::OnAdapterStateChanged(bluetooth::control::AdapterStatePtr state) {
  CLI_LOG() << "\n>>>> Active adapter state changed\n";
}

void App::OnDeviceDiscovered(bluetooth::control::RemoteDevicePtr device) {
  discovered_devices_[device->identifier] = std::move(device);
}

}  // namespace bluetoothcli
