// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fidl/test/bitsflexiblestrict/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::bitsflexiblestrict;

// [START contents]
fidl_test::Flags use_bits(fidl_test::Flags bits) {
  fidl_test::Flags result = bits | fidl_test::Flags::OPTION_A;
  auto truncated = fidl_test::Flags::TruncatingUnknown(uint32_t(result));
  ZX_ASSERT(!truncated.has_unknown_bits());

  result &= fidl_test::Flags::kMask;
  ZX_ASSERT(truncated == result);
  return result;
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
