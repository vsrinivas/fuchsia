// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/bitsflexiblestrict/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::bitsflexiblestrict;

// [START contents]
fidl_test::Flags use_bits(fidl_test::Flags bits) {
  fidl_test::Flags result = bits | fidl_test::Flags::OPTION_A;
  fidl_test::Flags mask = fidl_test::Flags::OPTION_A | fidl_test::Flags::OPTION_B;
  result &= mask;
  return result;
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
