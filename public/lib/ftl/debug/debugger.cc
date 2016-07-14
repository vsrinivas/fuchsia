// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "lib/ftl/debug/debugger.h"

namespace ftl {

void BreakDebugger() {
  // TODO(abarth): If we're being debugged, we should break into the debugger.
  abort();
}

}  // namespace ftl
