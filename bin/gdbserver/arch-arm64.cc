// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

int ComputeGdbSignal(const mx_exception_context_t& context) {
  FTL_NOTIMPLEMENTED();
  return -1;
}

bool IsSingleStepException(const mx_exception_context_t& context) {
  FTL_NOTIMPLEMENTED();
  return false;
}

void DumpArch(FILE* out) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace arch
}  // namespace debugserver
