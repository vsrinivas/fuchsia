// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <magenta/device/bt-hci.h>
#include <magenta/status.h>
#include <magenta/types.h>
#include <mx/channel.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/threading/create_thread.h"

namespace bluetooth {
namespace hci {

Transport::Transport(ftl::UniqueFD device_fd) : device_fd_(std::move(device_fd)) {}

Transport::~Transport() {
  if (is_running_) ShutDown();
}

bool Transport::Initialize() {
  FTL_DCHECK(device_fd_.get());
  FTL_DCHECK(!is_running_);

  // Obtain command channel handle.
  mx_handle_t handle = MX_HANDLE_INVALID;
  ssize_t ioctl_status = ioctl_bt_hci_get_command_channel(device_fd_.get(), &handle);
  if (ioctl_status < 0) {
    FTL_LOG(ERROR) << "hci: Failed to obtain command channel handle: "
                   << mx_status_get_string(ioctl_status);
    return false;
  }

  FTL_DCHECK(handle != MX_HANDLE_INVALID);
  is_running_ = true;
  io_thread_ = mtl::CreateThread(&io_task_runner_, "hci-transport");

  mx::channel channel(handle);
  command_channel_ = std::make_unique<CommandChannel>(this, std::move(channel));
  command_channel_->Initialize();

  return true;
}

bool Transport::InitializeForTesting(std::unique_ptr<CommandChannel> cmd_channel) {
  FTL_DCHECK(cmd_channel);
  FTL_DCHECK(!is_running_);

  is_running_ = true;
  io_thread_ = mtl::CreateThread(&io_task_runner_, "hci-transport-test");

  command_channel_ = std::move(cmd_channel);
  if (command_channel_) command_channel_->Initialize();

  return true;
}

void Transport::ShutDown() {
  FTL_DCHECK(is_running_);

  if (command_channel_) command_channel_->ShutDown();

  io_task_runner_->PostTask([] {
    FTL_DCHECK(mtl::MessageLoop::GetCurrent());
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });

  if (io_thread_.joinable()) io_thread_.join();

  command_channel_.reset();
  is_running_ = false;
  io_task_runner_ = nullptr;

  FTL_LOG(INFO) << "hci: Transport I/O loop exited";
}

}  // namespace hci
}  // namespace bluetooth
