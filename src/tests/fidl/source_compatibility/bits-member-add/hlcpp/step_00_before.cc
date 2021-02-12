// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/bitsmemberadd/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::bitsmemberadd;

// [START contents]
void use_member(fidl_test::Flags bits) {
  if (bits & fidl_test::Flags::OPTION_A) {
    printf("option A is set\n");
  }
  if (bits & fidl_test::Flags::OPTION_B) {
    printf("option B is set\n");
  }
  if (bits.has_unknown_bits()) {
    printf("unknown options: 0x%04x", uint32_t(bits.unknown_bits()));
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
