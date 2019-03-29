// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_HCI_TOOL_COMMANDS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_HCI_TOOL_COMMANDS_H_

#include <lib/async/dispatcher.h>

#include "src/connectivity/bluetooth/core/bt-host/hci/command_channel.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/connectivity/bluetooth/tools/lib/command_dispatcher.h"

namespace hcitool {

class CommandData final {
 public:
  CommandData(::bt::hci::CommandChannel* cmd_channel,
              async_dispatcher_t* dispatcher)
      : cmd_channel_(cmd_channel), dispatcher_(dispatcher) {}

  ::bt::hci::CommandChannel* cmd_channel() const { return cmd_channel_; }
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

 private:
  ::bt::hci::CommandChannel* cmd_channel_;
  async_dispatcher_t* dispatcher_;
};

void RegisterCommands(const CommandData* data,
                      ::bluetooth_tools::CommandDispatcher* dispatcher);

}  // namespace hcitool

#endif  // SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_HCI_TOOL_COMMANDS_H_
