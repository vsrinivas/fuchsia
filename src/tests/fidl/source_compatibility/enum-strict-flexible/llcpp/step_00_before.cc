// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/enumstrictflexible/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::enumstrictflexible;

// [START contents]
template <fidl_test::wire::Color color>
class ComplementaryColors {};

fidl_test::wire::Color writer(std::string s) {
  if (s == "red") {
    return fidl_test::wire::Color::RED;
  } else if (s == "blue") {
    return fidl_test::wire::Color::BLUE;
  } else {
    return fidl_test::wire::Color::UNKNOWN_COLOR;
  }
}

std::string reader(fidl_test::wire::Color color) {
  switch (color) {
    case fidl_test::wire::Color::RED:
      return "red";
    case fidl_test::wire::Color::BLUE:
      return "blue";
    case fidl_test::wire::Color::UNKNOWN_COLOR:
      return "unknown";
    default:
      return "error";
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
