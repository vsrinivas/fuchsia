// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <linenoise/linenoise.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

#include "commands.h"
#include "helpers.h"
#include "logging.h"

namespace bluetoothcli {

App::App()
    : context_(component::ApplicationContext::CreateFromStartupInfo()),
      manager_delegate_(this),
      adapter_delegate_(this) {
  FXL_DCHECK(context_);

  adapter_manager_ =
      context_
          ->ConnectToEnvironmentService<bluetooth_control::AdapterManager>();
  FXL_DCHECK(adapter_manager_);

  adapter_manager_.set_error_handler([] {
    CLI_LOG() << "AdapterManager disconnected";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  commands::RegisterCommands(this, &command_dispatcher_);

  // Register with the AdapterManager as its delegate.
  bluetooth_control::AdapterManagerDelegatePtr delegate;
  fidl::InterfaceRequest<bluetooth_control::AdapterManagerDelegate>
      delegate_request = delegate.NewRequest();
  manager_delegate_.Bind(std::move(delegate_request));
  adapter_manager_->SetDelegate(std::move(delegate));
}

void App::ReadNextInput() {
  bool call_complete_cb = true;
  auto complete_cb = [this] {
    async::PostTask(async_get_default(), [this] { ReadNextInput(); });
  };

  char* line = linenoise("bluetooth> ");
  if (!line) {
    fsl::MessageLoop::GetCurrent()->QuitNow();
    return;
  }

  auto ac = fxl::MakeAutoCall([&call_complete_cb, line, complete_cb] {
    if (call_complete_cb)
      complete_cb();
    free(line);
  });

  auto split =
      fxl::SplitStringCopy(fxl::StringView(line, std::strlen(line)), " ",
                           fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (split.empty() || split[0] == "help") {
    linenoiseHistoryAdd(line);
    command_dispatcher_.DescribeAllCommands();
    return;
  }

  bool cmd_found;
  if (!command_dispatcher_.ExecuteCommand(split, complete_cb, &cmd_found)) {
    if (!cmd_found)
      CLI_LOG() << "Unknown command: " << line;
    return;
  }

  call_complete_cb = false;
  linenoiseHistoryAdd(line);
}

void App::OnActiveAdapterChanged(
    bluetooth_control::AdapterInfoPtr active_adapter) {
  if (!active_adapter) {
    CLI_LOG() << "\n>>>> Active adapter is (null)";
    active_adapter_ = nullptr;
    return;
  }

  CLI_LOG() << "\n>>>> Active adapter: (id=" << active_adapter->identifier
            << ")\n";

  adapter_manager_->GetActiveAdapter(active_adapter_.NewRequest());

  bluetooth_control::AdapterDelegatePtr delegate;

  if (adapter_delegate_.is_bound()) {
    adapter_delegate_.Unbind();
  }
  adapter_delegate_.Bind(delegate.NewRequest());

  active_adapter_->SetDelegate(std::move(delegate));
}

void App::OnAdapterAdded(bluetooth_control::AdapterInfo adapter) {
  CLI_LOG() << "\n>>>> Adapter added (id=" << adapter.identifier << ")\n";
}

void App::OnAdapterRemoved(::fidl::StringPtr identifier) {
  CLI_LOG() << "\n>>>> Adapter removed (id=" << identifier << ")\n";
}

void App::OnAdapterStateChanged(bluetooth_control::AdapterState state) {
  CLI_LOG() << "\n>>>> Active adapter state changed\n";
}

void App::OnDeviceDiscovered(bluetooth_control::RemoteDevice device) {
  discovered_devices_[device.identifier] = std::move(device);
}

}  // namespace bluetoothcli
