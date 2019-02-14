// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace debug_ipc {

// This API controls and queries the debug functionality of the debug tools
// within the debug ipc.

// Activate this flag to activate debug output.
// False by default.
void SetDebugMode(bool);
bool IsDebugModeActive();

// Returns how many seconds have passed since the program started.
double SecondsSinceStart();

}  // namespace debug_ipc
