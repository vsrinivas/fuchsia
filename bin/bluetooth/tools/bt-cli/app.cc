// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <iostream>

#include <lib/async/cpp/task.h>
#include <linenoise/linenoise.h>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

#include "commands.h"
#include "helpers.h"
#include "logging.h"

namespace bluetoothcli {

App::App(async::Loop* loop)
    : context_(component::ApplicationContext::CreateFromStartupInfo()),
      control_delegate_(this),
      remote_device_delegate_(this),
      loop_(loop) {
  FXL_DCHECK(context_);

  control_ =
      context_->ConnectToEnvironmentService<bluetooth_control::Control>();
  FXL_DCHECK(control_);

  control_.set_error_handler([this]{
    CLI_LOG() << "Control disconnected";
    PostQuit();
  });

  commands::RegisterCommands(this, &command_dispatcher_);

  // Register with the Control as its delegate.
  bluetooth_control::ControlDelegatePtr delegate;
  fidl::InterfaceRequest<bluetooth_control::ControlDelegate> delegate_request =
      delegate.NewRequest();
  control_delegate_.Bind(std::move(delegate_request));
  control_->SetDelegate(std::move(delegate));

  // Register as a RemoteDeviceDelegate
  bluetooth_control::RemoteDeviceDelegatePtr remote_device_delegate;
  fidl::InterfaceRequest<bluetooth_control::RemoteDeviceDelegate>
      remote_device_delegate_request = remote_device_delegate.NewRequest();
  remote_device_delegate_.Bind(std::move(remote_device_delegate_request));
  control_->SetRemoteDeviceDelegate(std::move(remote_device_delegate), false);
}

void App::ReadNextInput() {
  bool call_complete_cb = true;
  auto complete_cb = [this] {
    async::PostTask(async(), [this] { ReadNextInput(); });
  };

  char* line = linenoise("bluetooth> ");
  if (!line) {
    Quit();
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
  if (split.empty()) {
    return;
  }

  bool cmd_found = false;
  if (command_dispatcher_.ExecuteCommand(split, complete_cb, &cmd_found)) {
    call_complete_cb = false;
  }

  if (!cmd_found) {
    CLI_LOG() << "Unknown command: " << line;
  } else {
    linenoiseHistoryAdd(line);
  }
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
}

void App::OnAdapterUpdated(bluetooth_control::AdapterInfo adapter) {
  CLI_LOG() << "\n>>>> Adapter updated (id=" << adapter.identifier << ")\n";
}

void App::OnAdapterRemoved(::fidl::StringPtr identifier) {
  CLI_LOG() << "\n>>>> Adapter removed (id=" << identifier << ")\n";
}

void App::OnDeviceUpdated(bluetooth_control::RemoteDevice device) {
  discovered_devices_[device.identifier] = std::move(device);
}

void App::OnDeviceRemoved(::fidl::StringPtr identifier) {
  discovered_devices_.erase(identifier);
}

void App::Quit() {
  loop_->Quit();
}

void App::PostQuit() {
  async::PostTask(async(), [this] {Quit();} );
}

}  // namespace bluetoothcli
