// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/library_zx.h"

#include <sstream>
// TODO(FIDL-478): make fidlc not depend on zircon
#include <zircon/types.h>

namespace fidl {
namespace LibraryZX {

const std::string kFilename = "zx.fidl";

namespace {
std::string GenerateData() {
  std::ostringstream out;

  // Please be sure to update the documentation about this library at the
  // location //docs/development/languages/fidl/reference/library-zx.md

  out <<
      R"FIDL(
[Internal]
library zx;

using status = int32;
using time = int64;
using duration = int64;
using koid = uint64;
using vaddr = uint64;
using paddr = uint64;
using paddr32 = uint32;
using gpaddr = uint64;
using off = uint64;
using procarg = uint32;
)FIDL";

  out << "const uint64 CHANNEL_MAX_MSG_BYTES = " << ZX_CHANNEL_MAX_MSG_BYTES << ";\n";
  out << "const uint64 CHANNEL_MAX_MSG_HANDLES = " << ZX_CHANNEL_MAX_MSG_HANDLES << ";\n";
  out << "const uint64 MAX_NAME_LEN = " << ZX_MAX_NAME_LEN << ";\n";
  out << "const uint64 MAX_CPUS = " << ZX_CPU_SET_MAX_CPUS << ";\n";

  return out.str();
}
}  // namespace

const std::string kData = GenerateData();

}  // namespace LibraryZX
}  // namespace fidl
