// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <lib/fostr/fidl/fuchsia/examples/formatting.h>
// [END includes]

#include <fuchsia/examples/cpp/fidl.h>

#include <gtest/gtest.h>

namespace {

// [START tests]
TEST(FidlExamples, Bits) {
  auto flags = fuchsia::examples::FileMode::READ | fuchsia::examples::FileMode::WRITE;
  std::cout << flags << std::endl;
}

TEST(FidlExamples, Enums) {
  auto enum_val = fuchsia::examples::LocationType::MUSEUM;
  std::cout << enum_val << std::endl;
}

TEST(FidlExamples, Structs) {
  fuchsia::examples::Color default_color;
  std::cout << default_color << std::endl;
}

TEST(FidlExamples, Unions) {
  auto int_val = fuchsia::examples::JsonValue::WithIntValue(1);
  std::cout << int_val << std::endl;
}
// [END tests]

}  // namespace
