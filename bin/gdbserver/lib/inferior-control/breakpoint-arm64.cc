// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

namespace {

// TODO(dje): This isn't needed until we support debugserver-injected
// breakpoints.
// The arm64 s/w breakpoint instruction.
//const uint32_t kBreakpoint = 0xd4200000;

}  // namespace

bool SoftwareBreakpoint::Insert() {
  FTL_NOTIMPLEMENTED();
  return false;
}

bool SoftwareBreakpoint::Remove() {
  FTL_NOTIMPLEMENTED();
  return false;
}

bool SoftwareBreakpoint::IsInserted() const {
  FTL_NOTIMPLEMENTED();
  return false;
}

bool SingleStepBreakpoint::Insert() {
  FTL_NOTIMPLEMENTED();
  return false;
}

bool SingleStepBreakpoint::Remove() {
  FTL_NOTIMPLEMENTED();
  return false;
}

bool SingleStepBreakpoint::IsInserted() const {
  FTL_NOTIMPLEMENTED();
  return false;
}

}  // namespace arch
}  // namespace debugserver
