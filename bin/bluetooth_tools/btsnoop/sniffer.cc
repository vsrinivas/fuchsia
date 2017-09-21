// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sniffer.h"

#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>

#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "lib/fxl/logging.h"

namespace btsnoop {

Sniffer::Sniffer(const std::string& hci_dev_path,
                 const std::string& log_file_path)
    : hci_dev_path_(hci_dev_path), log_file_path_(log_file_path) {}

Sniffer::~Sniffer() {
  if (wait_)
    wait_->Cancel();
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
    std::cout << "Failed to obtain snoop channel handle: "
              << zx_status_get_string(ioctl_status) << std::endl;
    return false;
  }

  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  if (!logger_.Initialize(log_file_path_)) {
    std::cout << "failed to initialize BTSnoop logger";
    return false;
  }

  wait_ = std::make_unique<async::AutoWait>(
      message_loop_.async(), handle,
      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  wait_->set_handler(fbl::BindMember(this, &Sniffer::OnHandleReady));
  zx_status_t status = wait_->Begin();
  if (status != ZX_OK) {
    std::cout << "Error on snoop channel: " << zx_status_get_string(status)
              << std::endl;
    wait_.reset(nullptr);
    message_loop_.QuitNow();
  }
  snoop_channel_ = zx::channel(handle);
  hci_dev_ = std::move(hci_dev);

  message_loop_.Run();

  return true;
}

async_wait_result_t Sniffer::OnHandleReady(async_t* async,
                                           zx_status_t wait_status,
                                           const zx_packet_signal_t* signal) {
  FXL_DCHECK(signal->observed & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED));

  if (wait_status != ZX_OK) {
    std::cout << "Error on snoop channel: " << zx_status_get_string(wait_status)
              << std::endl;
    message_loop_.QuitNow();
    return ASYNC_WAIT_FINISHED;
  }

  uint32_t read_size;
  zx_status_t status = snoop_channel_.read(0u, buffer_, sizeof(buffer_),
                                           &read_size, nullptr, 0, nullptr);
  if (status < 0) {
    std::cout << "Failed to read snoop event bytes: "
              << zx_status_get_string(status) << std::endl;
    message_loop_.QuitNow();
    return ASYNC_WAIT_FINISHED;
  }

  uint8_t flags = buffer_[0];
  logger_.WritePacket(bluetooth::common::BufferView(buffer_ + 1, read_size - 1),
                      flags & BT_HCI_SNOOP_FLAG_RECEIVED,
                      flags & BT_HCI_SNOOP_FLAG_DATA);
  return ASYNC_WAIT_AGAIN;
}

}  // namespace btsnoop
