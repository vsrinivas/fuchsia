// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/debug/debug.h"

#include <lib/zx/clock.h>

namespace debug_ipc {

namespace {

bool kDebugMode = false;

// This marks the moment SetDebugMode was called.
zx::time kStartTime = zx::clock::get_monotonic();

}  // namespace

bool IsDebugModeActive() { return kDebugMode; }

void SetDebugMode(bool activate) { kDebugMode = activate; }

double SecondsSinceStart() {
  return (zx::clock::get_monotonic() - kStartTime).to_nsecs() / 1000000000.0;
}

}  // namespace debug_ipc
