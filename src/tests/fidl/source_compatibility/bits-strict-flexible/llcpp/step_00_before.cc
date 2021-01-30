// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/bitsstrictflexible/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::bitsstrictflexible;

// [START contents]
uint32_t use_bits(fidl_test::Flags bits) {
  auto result = fidl_test::Flags::TruncatingUnknown(7u);
  if (bits & fidl_test::Flags::OPTION_A) {
    result |= fidl_test::Flags::kMask;
  }
  return uint32_t(result);
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
