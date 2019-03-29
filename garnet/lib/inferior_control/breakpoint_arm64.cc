// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/debugger_utils/breakpoints.h"

#include "breakpoint.h"

namespace inferior_control {

bool SingleStepBreakpoint::Insert() {
  FXL_NOTIMPLEMENTED();
  return false;
}

bool SingleStepBreakpoint::Remove() {
  FXL_NOTIMPLEMENTED();
  return false;
}

bool SingleStepBreakpoint::IsInserted() const {
  FXL_NOTIMPLEMENTED();
  return false;
}

}  // namespace inferior_control
