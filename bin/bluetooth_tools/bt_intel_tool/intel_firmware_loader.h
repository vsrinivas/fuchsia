// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// A loader for Intel Bluetooth Firmware files.

#include <deque>

#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

#include "command_channel.h"

namespace bt_intel {

class IntelFirmwareLoader {
 public:
  // |cmd_channel| is expected to outlive this object.
  IntelFirmwareLoader(CommandChannel* cmd_channel) : channel_(cmd_channel) {}
  ~IntelFirmwareLoader() = default;

  enum class LoadStatus {
    // Firmware is complete, no patch loaded, ready.
    kComplete,
    // Patch is loaded, reset the controller with patches enabled to continue
    kPatched,
    // An unexpected event was returned from the controller
    kError,
    // The file provided is in an invalid
    kInvalidFile,
  };

  // Reads and loads a "bseq" file into the controller using the given command
  // channel. Returns a LoadStatus indicating the result.
  //  - Complete if the firmware was loaded successfully
  //  - Patched if the firmware was loaded and a patch was added, meaning the
  //  controller should be reset.
  //  - Error otherwise.
  LoadStatus LoadBseq(const std::string& filename);

  // Reads and loads a "sfi" file into the controller using the given command
  // channel. Returns true if the file was loaded, false otherwise
  bool LoadSfi(const std::string& filename);

 private:
  bool ParseBseq();

  // Sends the next command and waits for the next events.
  // Returns true if the events returned matched the expected |event_bytes|,
  // false otherwise.
  bool RunCommandAndExpect(
      const ::btlib::common::PacketView<::btlib::hci::CommandHeader>& command,
      std::deque<::btlib::common::BufferView>& event_bytes);

  // The command channel to use
  CommandChannel* channel_;
};

}  // namespace bt_intel
