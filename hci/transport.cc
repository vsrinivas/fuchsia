// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <magenta/types.h>
#include <magenta/device/bt-hci.h>
#include <mx/channel.h>

#include "lib/ftl/logging.h"

#include <magenta/status.h>

namespace bluetooth {
namespace hci {

Transport::Transport(ftl::UniqueFD device_fd)
    : device_fd_(std::move(device_fd)) {}

Transport::~Transport() {
  ShutDown();
}

bool Transport::Initialize() {
  // Obtain command channel handle.
  mx_handle_t handle = MX_HANDLE_INVALID;
  ssize_t ioctl_status =
      ioctl_bt_hci_get_command_channel(device_fd_.get(), &handle);
  if (ioctl_status < 0) {
    FTL_LOG(ERROR) << "hci: Failed to obtain command channel handle: "
                   << mx_status_get_string(ioctl_status);
    return false;
  }

  FTL_DCHECK(handle != MX_HANDLE_INVALID);

  mx::channel channel(handle);
  command_channel_ = std::make_unique<CommandChannel>(std::move(channel));
  command_channel_->Initialize();

  return true;
}

void Transport::ShutDown() {
  if (command_channel_)
    command_channel_->ShutDown();
  command_channel_.reset();
}

}  // namespace hci
}  // namespace bluetooth
