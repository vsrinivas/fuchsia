// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/fxl/logging.h"

namespace debugserver {
namespace arch {

GdbSignal ComputeGdbSignal(const mx_exception_context_t& context) {
  FXL_NOTIMPLEMENTED();
  return GdbSignal::kUnsupported;
}

bool IsSingleStepException(const mx_exception_context_t& context) {
  FXL_NOTIMPLEMENTED();
  return false;
}

void DumpArch(FILE* out) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace arch
}  // namespace debugserver
