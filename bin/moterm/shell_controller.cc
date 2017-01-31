// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/shell_controller.h"

#include <string.h>

#include <sstream>

#include <magenta/processargs.h>
#include <magenta/syscalls/channel.h>

#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace moterm {

namespace {
constexpr char kShell[] = "file:///boot/bin/sh";
constexpr char kHistoryFilePath[] = "/data/moterm/history";
constexpr int kMaxHistorySize = 1000;
constexpr size_t kMaxHistoryEntrySize = 1024;

constexpr char kGetHistoryCommand[] = "get_history";
constexpr char kAddToHistoryCommand[] = "add_to_history:";

// Reads the command history. This uses a disk file as an intermediate step.
// TODO(ppi): read from Ledger instead.
std::deque<std::string> ReadHistory() {
  std::string content;
  if (!files::IsFile(kHistoryFilePath) ||
      !files::ReadFileToString(kHistoryFilePath, &content)) {
    return {};
  }

  std::vector<std::string> history = SplitStringCopy(
      content, "\n", ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);

  std::deque<std::string> result;
  std::move(std::begin(history), std::end(history), std::back_inserter(result));
  return result;
}

std::string SerializeHistory(const std::deque<std::string>& history) {
  std::stringstream output_stream;
  for (const std::string& command : history) {
    output_stream << command << std::endl;
  }

  return output_stream.str();
}

// Persists the command history. This uses a disk file as an intermediate step.
// TODO(ppi): write to Ledger instead.
void WriteHistory(const std::deque<std::string>& history) {
  if (!files::CreateDirectory(files::GetDirectoryName(kHistoryFilePath))) {
    FTL_LOG(ERROR) << "Unable to create directory for file "
                   << kHistoryFilePath;
    return;
  }

  const std::string output = SerializeHistory(history);
  if (!files::WriteFile(kHistoryFilePath, output.c_str(), output.size())) {
    FTL_LOG(ERROR) << "Unable to write terminal history to "
                   << kHistoryFilePath;
    return;
  }
}
}  // namespace

ShellController::ShellController() {}

ShellController::~ShellController() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
  }
  WriteHistory(terminal_history_);
}

std::vector<std::string> ShellController::GetShellCommand() {
  return {std::string(kShell)};
}

std::vector<mtl::StartupHandle> ShellController::GetStartupHandles() {
  std::vector<mtl::StartupHandle> ret;

  mx::channel shell_handle;
  mx_status_t status = mx::channel::create(0, &channel_, &shell_handle);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create an mx::channel for the shell, status: "
                   << status;
    return {};
  }
  mtl::StartupHandle startup_handle;
  startup_handle.id = MX_HND_TYPE_USER1;
  startup_handle.handle = std::move(shell_handle);
  ret.push_back(std::move(startup_handle));

  return ret;
}

void ShellController::Start() {
  terminal_history_ = ReadHistory();
  WaitForShell();
}

bool ShellController::HandleGetHistory() {
  const std::string history_str = SerializeHistory(terminal_history_);

  mx::vmo data;
  if (!mtl::VmoFromString(history_str, &data)) {
    FTL_LOG(ERROR) << "Failed to write terminal history to a vmo.";
    return false;
  }

  const mx_handle_t handles[] = {data.release()};
  const std::string command = "";
  mx_status_t status =
      channel_.write(0, command.data(), command.size(), handles, 1);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR)
        << "Failed to write the terminal history response to channel.";
    mx_handle_close(handles[0]);
    return false;
  }
  return true;
}

void ShellController::HandleAddToHistory(const std::string& entry) {
  terminal_history_.push_back(entry);
  if (terminal_history_.size() > kMaxHistorySize) {
    terminal_history_.pop_front();
  }
}

void ShellController::ReadCommand() {
  // The commands should not be bigger than the name of the command + max size
  // of a history entry.
  char buffer[kMaxHistoryEntrySize + 100];
  uint32_t num_bytes = 0;
  mx_status_t rv =
      channel_.read(MX_CHANNEL_READ_MAY_DISCARD, buffer, sizeof(buffer),
                    &num_bytes, nullptr, 0, nullptr);
  if (rv == NO_ERROR) {
    const std::string command = std::string(buffer, num_bytes);
    if (command == kGetHistoryCommand) {
      if (!HandleGetHistory()) {
        return;
      }
    } else if (command.substr(0, strlen(kAddToHistoryCommand)) ==
               kAddToHistoryCommand) {
      HandleAddToHistory(command.substr(strlen(kAddToHistoryCommand)));
    } else {
      FTL_LOG(ERROR) << "Unrecognized shell command: " << command;
      return;
    }

    WaitForShell();
  } else if (rv == ERR_SHOULD_WAIT) {
    WaitForShell();
  } else if (rv == ERR_BUFFER_TOO_SMALL) {
    // Ignore the command.
    FTL_LOG(WARNING) << "The command sent by shell didn't fit in the buffer.";
  } else if (rv == ERR_REMOTE_CLOSED) {
    channel_.reset();
    return;
  } else {
    FTL_DCHECK(false) << "Unhandled mx_status_t: " << rv;
  }
}

void ShellController::WaitForShell() {
  FTL_DCHECK(!wait_id_);
  wait_id_ = waiter_->AsyncWait(channel_.get(),
                                MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                MX_TIME_INFINITE, &WaitComplete, this);
}

// static.
void ShellController::WaitComplete(mx_status_t result,
                                   mx_signals_t pending,
                                   void* context) {
  ShellController* controller = static_cast<ShellController*>(context);
  controller->wait_id_ = 0;
  controller->ReadCommand();
}

}  // namespace moterm
