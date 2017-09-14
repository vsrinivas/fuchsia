// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_wrapper.h"

#include <zircon/device/bt-hci.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "lib/fxl/logging.h"

namespace bluetooth {
namespace hci {

ZirconDeviceWrapper::ZirconDeviceWrapper(fxl::UniqueFD device_fd)
    : device_fd_(std::move(device_fd)) {
  FXL_DCHECK(device_fd_.is_valid());
}

zx::channel ZirconDeviceWrapper::GetCommandChannel() {
  zx::channel channel;
  ssize_t status = ioctl_bt_hci_get_command_channel(device_fd_.get(), channel.reset_and_get_address());
  if (status < 0) {
    FXL_LOG(ERROR) << "hci: Failed to obtain command channel handle: "
                   << zx_status_get_string(status);
    FXL_DCHECK(!channel.is_valid());
  }

  return channel;
}

zx::channel ZirconDeviceWrapper::GetACLDataChannel() {
  zx::channel channel;
  ssize_t status = ioctl_bt_hci_get_acl_data_channel(device_fd_.get(), channel.reset_and_get_address());
  if (status < 0) {
    FXL_LOG(ERROR) << "hci: Failed to obtain ACL data channel handle: "
                   << zx_status_get_string(status);
    FXL_DCHECK(!channel.is_valid());
  }

  return channel;
}

// ================= DummyDeviceWrappper =================

DummyDeviceWrapper::DummyDeviceWrapper(zx::channel cmd_channel, zx::channel acl_data_channel)
    : cmd_channel_(std::move(cmd_channel)), acl_data_channel_(std::move(acl_data_channel)) {}

zx::channel DummyDeviceWrapper::GetCommandChannel() {
  return std::move(cmd_channel_);
}

zx::channel DummyDeviceWrapper::GetACLDataChannel() {
  return std::move(acl_data_channel_);
}

}  // namespace hci
}  // namespace bluetooth
