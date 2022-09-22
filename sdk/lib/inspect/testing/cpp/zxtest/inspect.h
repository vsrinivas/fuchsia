// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_TESTING_CPP_ZXTEST_INSPECT_H_
#define LIB_INSPECT_TESTING_CPP_ZXTEST_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <zxtest/zxtest.h>

namespace inspect {

// Helper class to test inspect data with zxtest framework.
// Example:
//
//  using inspect::InspectTestHelper;
//  class ExampleTest : inspect::InspectTestHelper, public zxtest::Test {
//    public:
//    zx::vmo& inspect_vmo() { // returns inspect vmo}
//    inspect::Inspector& inspector() { // returns inspector reference }
//  };
//
//  TEST_F(ExampleTest, CheckInspectVmo) {
//    ASSERT_NO_FATAL_FAILURE(ReadInspect(inspect_vmo()));
//    CheckProperty(
//      hierarchy().node(), "root_property1", inspect::StringPropertyValue("OK")));
//    auto* genX_node = hierarchy().GetByPath({"child", "grandchild"});
//    ASSERT_TRUE(genX_node);
//    CheckProperty(
//      genX_node->node(), "genX_status", inspect::IntPropertyValue(ZX_OK)));
//  }
class InspectTestHelper {
 public:
  InspectTestHelper() = default;

  // Helper method to read from a VMO.
  // Note: ReadInspect needs to be called everytime inspect data is changed for the changes to be
  // reflected in the hierarchy.
  void ReadInspect(const zx::vmo& vmo);

  // Helper method to read from inspect::Inspector.
  // Note:
  //   - ReadInspect needs to be called everytime inspect data is changed for the changes to be
  //     reflected in the hierarchy.
  //   - This method processes inspect tree on the calling thread. If the inspector has lazy nodes
  //     that rely on other dispatchers, this method should not be used.
  void ReadInspect(const inspect::Inspector& inspector);

  inspect::Hierarchy& hierarchy() { return hierarchy_.value(); }

  template <typename T>
  static void CheckProperty(const inspect::NodeValue& node, std::string property,
                            T expected_value) {
    const T* actual_value = node.get_property<T>(property);
    EXPECT_TRUE(actual_value);
    if (!actual_value) {
      return;
    }
    EXPECT_EQ(expected_value.value(), actual_value->value());
  }

  // Printers for debugging purpose.
  static void PrintAllProperties(const inspect::NodeValue& node);
  static void PrintAllProperties(const inspect::Hierarchy& hierarchy);

 private:
  fpromise::result<inspect::Hierarchy> hierarchy_;
};
}  // namespace inspect

#endif  // LIB_INSPECT_TESTING_CPP_ZXTEST_INSPECT_H_
