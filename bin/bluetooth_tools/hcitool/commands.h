// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/macros.h"

namespace hcitool {

class CommandDispatcher;

void RegisterCommands(CommandDispatcher* handler_map);

}  // namespace hcitool
