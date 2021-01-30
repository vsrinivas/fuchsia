// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/enumstrictflexible/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::enumstrictflexible;

// [START contents]
template <fidl_test::Color color>
class ComplementaryColors {};

fidl_test::Color writer(std::string s) {
  if (s == "red") {
    return fidl_test::Color::RED;
  } else if (s == "blue") {
    return fidl_test::Color::BLUE;
  } else {
    return fidl_test::Color::UNKNOWN_COLOR;
  }
}

std::string reader(fidl_test::Color color) {
  switch (color) {
    case fidl_test::Color::RED:
      return "red";
    case fidl_test::Color::BLUE:
      return "blue";
    case fidl_test::Color::UNKNOWN_COLOR:
      return "unknown";
    default:
      return "error";
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
