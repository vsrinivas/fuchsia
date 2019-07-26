// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMANDS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMANDS_H_

#include "src/connectivity/bluetooth/tools/lib/command_dispatcher.h"

#include "command_channel.h"

namespace bt_intel {

void RegisterCommands(CommandChannel* data, ::bluetooth_tools::CommandDispatcher* dispatcher);

}  // namespace bt_intel

#endif  // SRC_CONNECTIVITY_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMANDS_H_
