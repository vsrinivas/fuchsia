// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.enummemberremove/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_enummemberremove;

// [START contents]
fidl_test::wire::Color writer(std::string s) {
  if (s == "red") {
    return fidl_test::wire::Color::kRed;
  } else if (s == "blue") {
    return fidl_test::wire::Color::kBlue;
  } else {
    return fidl_test::wire::Color::Unknown();
  }
}

std::string reader(fidl_test::wire::Color color) {
  switch (color) {
    case fidl_test::wire::Color::kRed:
      return "red";
    case fidl_test::wire::Color::kBlue:
      return "blue";
    default:
      return "<unknown>";
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
