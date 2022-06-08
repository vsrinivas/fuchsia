// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "util.h"

namespace debugger_utils {

std::string ZxErrorString(zx_status_t status) {
  return fxl::StringPrintf("%s(%d)", zx_status_get_string(status), status);
}

}  // namespace debugger_utils
