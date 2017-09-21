// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/macros.h"

namespace bluetooth {
namespace tools {

class CommandDispatcher;

}  // namespace tools
}  // namespace bluetooth

namespace bluetoothcli {

class App;

namespace commands {

void RegisterCommands(App* app,
                      bluetooth::tools::CommandDispatcher* dispatcher);

}  // namespace commands
}  // namespace bluetoothcli
