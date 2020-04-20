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
  // exceeded. See KernelCmdlineTest.AlmostMaximumExpansion and
  // KernelCmdlineTest.MaximumExpansion for explanation of the limit.
  constexpr size_t kMaxInputSize = 2729;
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
