// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "apps/bluetooth/lib/common/bt_snoop_logger.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace btsnoop {

class Sniffer final : public ::fsl::MessageLoopHandler {
 public:
  Sniffer(const std::string& hci_dev_path, const std::string& log_file_path);
  ~Sniffer();

  // Starts the packet sniffing loop. Returns false if there is an error while
  // setting up the snoop file and device snoop channel.
  bool Start();

 private:
  // ::fsl::MessageLoopHandler overrides:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count) override;
  void OnHandleError(zx_handle_t handle, zx_status_t error) override;

  std::string hci_dev_path_;
  std::string log_file_path_;

  fxl::UniqueFD hci_dev_;
  zx::channel snoop_channel_;
  bluetooth::common::BTSnoopLogger logger_;

  fsl::MessageLoop::HandlerKey handler_key_;
  fsl::MessageLoop message_loop_;

  // For now we only sniff command and event packets so make the buffer large
  // enough to fit the largest command packet plus 1-byte for the snoop flags.
  uint8_t buffer_[sizeof(bluetooth::hci::CommandHeader) +
                  bluetooth::hci::kMaxCommandPacketPayloadSize +
                  1];

  FXL_DISALLOW_COPY_AND_ASSIGN(Sniffer);
};

}  // namespace btsnoop
