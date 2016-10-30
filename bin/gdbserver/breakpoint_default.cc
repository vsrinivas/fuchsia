// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

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

}  // namespace arch
}  // namespace debugserver
