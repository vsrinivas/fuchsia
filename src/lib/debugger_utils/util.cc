// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace debugger_utils {

std::string ErrnoString(int err) { return fxl::StringPrintf("%s(%d)", strerror(err), err); }

}  // namespace debugger_utils
