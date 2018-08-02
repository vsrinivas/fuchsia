// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/library_zx.h"

namespace fidl {
namespace LibraryZX {

const char kFilename[] = "zx.fidl";

const char kData[] = R"FIDL(
[Internal]
library zx;

using @status = int32;
using time = uint64;
using duration = uint64;
using koid = uint64;
using vaddr = uint64;
using paddr = uint64;
using paddr32 = uint32;
using off = uint64;
)FIDL";

} // namespace LibraryZX
} // namespace fidl
