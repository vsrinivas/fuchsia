// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.enumflexiblestrict/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_enumflexiblestrict;

// [START contents]
fidl_test::wire::Color complement(fidl_test::wire::Color color) {
  if (color.IsUnknown()) {
    return color;
  }
  switch (color) {
    case fidl_test::wire::Color::kRed:
      return fidl_test::wire::Color::kBlue;
    case fidl_test::wire::Color::kBlue:
      return fidl_test::wire::Color::kRed;
    default:
      return color;
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
