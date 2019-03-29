// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/debug/debug.h"

#include "src/lib/fxl/time/time_point.h"

namespace debug_ipc {

namespace {

bool kDebugMode = false;

// This marks the moment SetDebugMode was called.
fxl::TimePoint kStartTime = fxl::TimePoint::Now();

}  // namespace

bool IsDebugModeActive() { return kDebugMode; }

void SetDebugMode(bool activate) { kDebugMode = activate; }

double SecondsSinceStart() {
  return (fxl::TimePoint::Now() - kStartTime).ToSecondsF();
}

}  // namespace debug_ipc
