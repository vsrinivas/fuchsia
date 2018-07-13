// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include "lib/fxl/logging.h"

namespace inferior_control {

namespace {

// TODO(dje): This isn't needed until we support debugserver-injected
// breakpoints.
// The arm64 s/w breakpoint instruction.
// const uint32_t kBreakpoint = 0xd4200000;

}  // namespace

bool SoftwareBreakpoint::Insert() {
  FXL_NOTIMPLEMENTED();
  return false;
}

bool SoftwareBreakpoint::Remove() {
  FXL_NOTIMPLEMENTED();
  return false;
}

bool SoftwareBreakpoint::IsInserted() const {
  FXL_NOTIMPLEMENTED();
  return false;
}

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
