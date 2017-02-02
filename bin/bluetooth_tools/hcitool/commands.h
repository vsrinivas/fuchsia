// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/macros.h"

#include "command_handler.h"

namespace hcitool {

class CommandHandlerMap;

void RegisterCommands(CommandHandlerMap* handler_map);

HCITOOL_DEFINE_HANDLER(ResetHandler, "reset")
HCITOOL_DEFINE_HANDLER(ReadBDADDRHandler, "read-bdaddr")
HCITOOL_DEFINE_HANDLER(ReadLocalNameHandler, "read-local-name")
HCITOOL_DEFINE_HANDLER(WriteLocalNameHandler, "write-local-name")

}  // namespace hcitool
