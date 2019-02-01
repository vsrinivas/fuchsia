// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/fxl/logging.h"

namespace inferior_control {

GdbSignal ComputeGdbSignal(const zx_exception_context_t& context) {
  FXL_NOTIMPLEMENTED();
  return GdbSignal::kUnsupported;
}

bool IsSingleStepException(const zx_exception_context_t& context) {
  FXL_NOTIMPLEMENTED();
  return false;
}

void DumpArch(FILE* out) { FXL_NOTIMPLEMENTED(); }

}  // namespace inferior_control
