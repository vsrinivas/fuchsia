// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "banjo/library_zx.h"

namespace banjo {
namespace LibraryZX {

const char kFilename[] = "zx.banjo";

const char kData[] = R"BANJO(
[Internal]
library zx;

using @status = int32;
using time = int64;
using duration = int64;
using koid = uint64;
using vaddr = uint64;
using paddr = uint64;
using paddr32 = uint32;
using gpaddr = uint64;
using off = uint64;
)BANJO";

} // namespace LibraryZX
} // namespace banjo
