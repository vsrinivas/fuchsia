// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/enumflexiblestrict/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::enumflexiblestrict;

// [START contents]
fidl_test::Color complement(fidl_test::Color color) {
  switch (color) {
    case fidl_test::Color::RED:
      return fidl_test::Color::BLUE;
    case fidl_test::Color::BLUE:
      return fidl_test::Color::RED;
    default:
      return color;
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
