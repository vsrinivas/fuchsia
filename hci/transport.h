// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/bluetooth/hci/command_channel.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"

namespace bluetooth {
namespace hci {

// Represents the HCI transport layer. This object owns the HCI command, ACL,
// and SCO channels and provides the necessary control-flow mechanisms to send
// and receive HCI packets from the underlying Bluetooth controller.
class Transport final {
 public:
  // |device_fd| must be a valid file descriptor to a Bluetooth HCI device.
  explicit Transport(ftl::UniqueFD device_fd);
  ~Transport();

  // Initializes the transport channels. Returns false if an error occurs.
  bool Initialize();

  // Cleans up the transport channels.
  void ShutDown();

  // Returns a pointer to the HCI command and event control-flow handler.
  CommandChannel* command_channel() const { return command_channel_.get(); }

 private:
  // The Bluetooth HCI device file descriptor.
  ftl::UniqueFD device_fd_;

  // The HCI command and event control-flow handler.
  std::unique_ptr<CommandChannel> command_channel_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace hci
}  // namespace bluetooth
