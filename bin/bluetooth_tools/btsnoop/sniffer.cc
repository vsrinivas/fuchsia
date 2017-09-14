// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sniffer.h"

#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>

#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "lib/fxl/logging.h"

namespace btsnoop {

Sniffer::Sniffer(const std::string& hci_dev_path, const std::string& log_file_path)
    : hci_dev_path_(hci_dev_path), log_file_path_(log_file_path), handler_key_(0) {}

Sniffer::~Sniffer() {
  message_loop_.RemoveHandler(handler_key_);
}

bool Sniffer::Start() {
  fxl::UniqueFD hci_dev(open(hci_dev_path_.c_str(), O_RDWR));
  if (!hci_dev.is_valid()) {
    std::perror("Failed to open HCI device");
    return false;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ssize_t ioctl_status = ioctl_bt_hci_get_snoop_channel(hci_dev.get(), &handle);
  if (ioctl_status < 0) {
    std::cout << "Failed to obtain snoop channel handle: " << zx_status_get_string(ioctl_status)
              << std::endl;
    return false;
  }

  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  if (!logger_.Initialize(log_file_path_)) {
    std::cout << "failed to initialize BTSnoop logger";
    return false;
  }

  handler_key_ =
      message_loop_.AddHandler(this, handle, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  snoop_channel_ = zx::channel(handle);
  hci_dev_ = std::move(hci_dev);

  message_loop_.Run();

  return true;
}

void Sniffer::OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count) {
  FXL_DCHECK(handle == snoop_channel_.get());
  FXL_DCHECK(pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED));

  uint32_t read_size;
  zx_status_t status =
      snoop_channel_.read(0u, buffer_, sizeof(buffer_), &read_size, nullptr, 0, nullptr);
  if (status < 0) {
    std::cout << "Failed to read snoop event bytes: " << zx_status_get_string(status) << std::endl;
    message_loop_.QuitNow();
    return;
  }

  uint8_t flags = buffer_[0];
  logger_.WritePacket(bluetooth::common::BufferView(buffer_ + 1, read_size - 1),
                      flags & BT_HCI_SNOOP_FLAG_RECEIVED, flags & BT_HCI_SNOOP_FLAG_DATA);
}

void Sniffer::OnHandleError(zx_handle_t handle, zx_status_t error) {
  FXL_DCHECK(handle == snoop_channel_.get());
  std::cout << "Error on snoop channel: " << zx_status_get_string(error) << std::endl;
  message_loop_.QuitNow();
}

}  // namespace btsnoop
