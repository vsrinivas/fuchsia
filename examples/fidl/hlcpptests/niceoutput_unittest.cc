// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/examples/formatting.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/builder.h"

namespace fuchsia {
namespace examples {

template <typename T>
bool operator==(const T& lhs, const T& rhs) { return ::fidl::Equals(lhs, rhs); }

// By using fostr, the << operator is automatically overloaded in the proper
// namespace. Otherwise, you can define a custom operator as so:
//
// std::ostream& operator<<(std::ostream& os, const Car& value) {
//   return os << "Car(num_wheels=" << value.num_wheels << ")";
// }

}  // namespace examples
}  // namespace fuchsia

namespace {

TEST(NiceOutput, ExampleFailure) {
  using namespace fuchsia::examples;

  Car actual;
  Wheel actual_wheel1;
  actual_wheel1.brand = "Superfast";
  Wheel actual_wheel2;
  actual_wheel2.brand = "Notsofast";
  actual.wheels.emplace_back(std::move(actual_wheel1));
  actual.wheels.emplace_back(std::move(actual_wheel2));

  Car expected;

  EXPECT_EQ(actual, expected);
}

}  // namespace
