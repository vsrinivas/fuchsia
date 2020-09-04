// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/check.h"

#include <stdio.h>
#include <stdlib.h>

namespace fidl {

void LogMessageAndAbort(const char* file, int line, const char* condition, const char* message) {
  fprintf(stderr, "Check failed: %s\n%s:%d: %s\n", condition, file, line, message);
  abort();
}

}
