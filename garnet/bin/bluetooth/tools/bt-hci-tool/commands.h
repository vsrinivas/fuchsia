// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

#include "garnet/bin/bluetooth/tools/lib/command_dispatcher.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace hcitool {

class CommandData final {
 public:
  CommandData(::btlib::hci::CommandChannel* cmd_channel,
              async_dispatcher_t* dispatcher)
      : cmd_channel_(cmd_channel), dispatcher_(dispatcher) {}

  ::btlib::hci::CommandChannel* cmd_channel() const { return cmd_channel_; }
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

 private:
  ::btlib::hci::CommandChannel* cmd_channel_;
  async_dispatcher_t* dispatcher_;
};

void RegisterCommands(const CommandData* data,
                      ::bluetooth_tools::CommandDispatcher* dispatcher);

}  // namespace hcitool
