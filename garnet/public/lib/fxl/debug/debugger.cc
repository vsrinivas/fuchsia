// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "lib/fxl/debug/debugger.h"

namespace fxl {

void BreakDebugger() {
  // TODO(abarth): If we're being debugged, we should break into the debugger.
  abort();
}

}  // namespace fxl
