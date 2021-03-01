// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/enumflexiblestrict/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::enumflexiblestrict;

// [START contents]
fidl_test::wire::Color complement(fidl_test::wire::Color color) {
  switch (color) {
    case fidl_test::wire::Color::RED:
      return fidl_test::wire::Color::BLUE;
    case fidl_test::wire::Color::BLUE:
      return fidl_test::wire::Color::RED;
    default:
      return color;
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
