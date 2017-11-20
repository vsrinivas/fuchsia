// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>
#include "lib/fxl/time/time_delta.h"

namespace escher {
namespace test {

// How long to run the message loop when we want to allow a task in the
// task queue to run.
constexpr fxl::TimeDelta kPumpMessageLoopDuration =
    fxl::TimeDelta::FromMilliseconds(16);

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create a duplicate of the event.
zx::event CopyEvent(const zx::event& event);

}  // namespace test
}  // namespace escher
