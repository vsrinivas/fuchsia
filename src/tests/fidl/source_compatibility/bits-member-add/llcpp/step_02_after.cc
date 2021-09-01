// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.bitsmemberadd/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_bitsmemberadd;

// [START contents]
void use_bits(fidl_test::wire::Flags bits) {
  if (bits & fidl_test::wire::Flags::kOptionA) {
    printf("option A is set\n");
  }
  if (bits & fidl_test::wire::Flags::kOptionB) {
    printf("option B is set\n");
  }
  if (bits & fidl_test::wire::Flags::kOptionC) {
    printf("option C is set\n");
  }
  if (bits.has_unknown_bits()) {
    printf("unknown options: 0x%04x", uint32_t(bits.unknown_bits()));
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
