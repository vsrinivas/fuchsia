// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/shell_controller.h"

#include <string.h>

#include <sstream>

#include <zircon/processargs.h>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

namespace moterm {

namespace {
constexpr char kShell[] = "/boot/bin/sh";
constexpr size_t kMaxHistoryEntrySize = 1024;

constexpr char kGetHistoryCommand[] = "get_history";
constexpr char kAddLocalEntryCommand[] = "add_local_entry:";
constexpr char kAddRemoteEntryCommand[] = "add_remote_entry:";

std::string SerializeHistory(const std::vector<std::string>& history) {
  std::stringstream output_stream;
  for (const std::string& command : history) {
    output_stream << command << std::endl;
  }

  return output_stream.str();
}

}  // namespace

ShellController::ShellController(History* history) : history_(history) {
  history_->RegisterClient(this);
}

ShellController::~ShellController() {}

std::vector<std::string> ShellController::GetShellCommand() {
  return {std::string(kShell)};
}

std::vector<fsl::StartupHandle> ShellController::GetStartupHandles() {
  std::vector<fsl::StartupHandle> ret;

  zx::channel shell_handle;
  zx_status_t status = zx::channel::create(0, &channel_, &shell_handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create an zx::channel for the shell, status: "
                   << status;
    return {};
  }
  fsl::StartupHandle startup_handle;
  startup_handle.id = PA_USER1;
  startup_handle.handle = std::move(shell_handle);
  ret.push_back(std::move(startup_handle));

  return ret;
}

void ShellController::Start() {
  WaitForShell();
}

// Stops communication with the shell.
void ShellController::Terminate() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
  history_->UnregisterClient(this);
}

void ShellController::OnRemoteEntry(const std::string& entry) {
  // Ignore entries that are too big for the controller protocol to handle.
  if (entry.size() > kMaxHistoryEntrySize) {
    return;
  }
  std::string command = kAddRemoteEntryCommand + entry;
  zx_status_t status =
      channel_.write(0, command.data(), command.size(), nullptr, 0);
  if (status != ZX_OK && status != ZX_ERR_NO_MEMORY) {
    FXL_LOG(ERROR) << "Failed to write a " << kAddRemoteEntryCommand
                   << " command, status: " << status;
  }
}

bool ShellController::SendBackHistory(std::vector<std::string> entries) {
  const std::string history_str = SerializeHistory(entries);

  zx::vmo data;
  if (!fsl::VmoFromString(history_str, &data)) {
    FXL_LOG(ERROR) << "Failed to write terminal history to a vmo.";
    return false;
  }

  const zx_handle_t handles[] = {data.release()};
  const std::string command = "";
  zx_status_t status =
      channel_.write(0, command.data(), command.size(), handles, 1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "Failed to write the terminal history response to channel.";
    zx_handle_close(handles[0]);
    return false;
  }
  return true;
}

void ShellController::HandleAddToHistory(const std::string& entry) {
  history_->AddEntry(entry);
}

void ShellController::ReadCommand() {
  // The commands should not be bigger than the name of the command + max size
  // of a history entry.
  char buffer[kMaxHistoryEntrySize + 100];
  uint32_t num_bytes = 0;
  zx_status_t rv =
      channel_.read(ZX_CHANNEL_READ_MAY_DISCARD, buffer, sizeof(buffer),
                    &num_bytes, nullptr, 0, nullptr);
  if (rv == ZX_OK) {
    const std::string command = std::string(buffer, num_bytes);
    if (command == kGetHistoryCommand) {
      history_->ReadInitialEntries([this](std::vector<std::string> entries) {
        SendBackHistory(std::move(entries));
      });
    } else if (command.substr(0, strlen(kAddLocalEntryCommand)) ==
               kAddLocalEntryCommand) {
      HandleAddToHistory(command.substr(strlen(kAddLocalEntryCommand)));
    } else {
      FXL_LOG(ERROR) << "Unrecognized shell command: " << command;
      return;
    }

    WaitForShell();
  } else if (rv == ZX_ERR_SHOULD_WAIT) {
    WaitForShell();
  } else if (rv == ZX_ERR_BUFFER_TOO_SMALL) {
    // Ignore the command.
    FXL_LOG(WARNING) << "The command sent by shell didn't fit in the buffer.";
  } else if (rv == ZX_ERR_PEER_CLOSED) {
    channel_.reset();
    return;
  } else {
    FXL_DCHECK(false) << "Unhandled zx_status_t: " << rv;
  }
}

void ShellController::WaitForShell() {
  FXL_DCHECK(!wait_id_);
  wait_id_ = waiter_->AsyncWait(channel_.get(),
                                ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &WaitComplete, this);
}

// static.
void ShellController::WaitComplete(zx_status_t result,
                                   zx_signals_t pending,
                                   uint64_t count,
                                   void* context) {
  ShellController* controller = static_cast<ShellController*>(context);
  controller->wait_id_ = 0;
  controller->ReadCommand();
}

}  // namespace moterm
