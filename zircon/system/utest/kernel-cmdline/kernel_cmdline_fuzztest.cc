// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "lib/cmdline.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Break input up into 2 halves, one to pass to Append(), and the second to do
  // lookups.
  std::string input(data, data + size / 2);
  std::string lookups(data + size / 2, data + size);

  // The rounding is tricky here, so this bound will need to be updated if this
  // maximum changes.
  static_assert(Cmdline::kCmdlineMax == 4096, "need to update early out below");
  // Limit the input size, because Cmdline will intentionally panic if its limit
  // exceeded. The maximum expansion happens in the case of an input string
  // like: [a a a ...], which turns into [a=\0a=\0a=\0...], that is, an
  // expansion by a third. Additionally, there's an extra trailing terminator
  // that must fit into the buffer.
  //
  // Two-thirds of 4096 is 1365 1/3, so, the maximum possible number of "a " is
  // 1365*2 = 2730. Each "a " turns into "a=\0", for a total of 1365*3=4095
  // long. So, there's already one extra space available for the extra
  // terminator due to rounding, so there's no need to subtract one more.
  //
  // See KernelCmdLineTest.MaximumExpansion for a unittest of this logic.
  constexpr size_t kMaxInputSize = 2730;
  if (input.size() > kMaxInputSize) {
    return 0;
  }

  Cmdline c;
  c.Append(input.c_str());

  const char* p = lookups.c_str();
  for (;;) {
    c.GetString(p);
    p = strchr(p, 0) + 1;
    if (p >= &lookups[lookups.size()]) {
      break;
    }
  }

  return 0;
}
