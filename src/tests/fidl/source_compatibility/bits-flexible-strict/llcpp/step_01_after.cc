// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.bitsflexiblestrict/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_bitsflexiblestrict;

// [START contents]
uint32_t use_bits(fidl_test::wire::Flags bits) {
  auto result = fidl_test::wire::Flags::TruncatingUnknown(7u);
  if (bits & fidl_test::wire::Flags::kOptionA) {
    result |= fidl_test::wire::Flags::kMask;
  }
  return uint32_t(result);
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
