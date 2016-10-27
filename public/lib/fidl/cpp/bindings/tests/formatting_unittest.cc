// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/formatting.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "mojo/public/interfaces/bindings/tests/test_structs.mojom.h"

namespace fidl {
namespace test {
namespace {
RectPtr MakeRect(int32_t factor = 1) {
  RectPtr rect(Rect::New());
  rect->x = 1 * factor;
  rect->y = 2 * factor;
  rect->width = 10 * factor;
  rect->height = 20 * factor;
  return rect;
}
}  // namespace

std::ostream& operator<<(std::ostream& os, const Rect& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const RectPair& value) {
  return os << "{first=" << value.first << ", second=" << value.second << "}";
}

TEST(FormattingTest, Arrays) {
  Array<int32_t> null_array;
  Array<int32_t> empty_array;
  empty_array.resize(0);
  Array<int32_t> one_element_array;
  one_element_array.push_back(123);
  Array<int32_t> three_element_array;
  three_element_array.push_back(4);
  three_element_array.push_back(5);
  three_element_array.push_back(6);

  std::ostringstream so;
  so << "null_array=" << null_array << ", empty_array=" << empty_array
     << ", one_element_array=" << one_element_array
     << ", three_element_array=" << three_element_array;

  EXPECT_EQ(
      "null_array=null, "
      "empty_array=[], "
      "one_element_array=[123], "
      "three_element_array=[4, 5, 6]",
      so.str());
}

TEST(FormattingTest, Maps) {
  Map<int32_t, std::string> null_map;
  Map<int32_t, std::string> empty_map;
  empty_map.mark_non_null();
  Map<int32_t, std::string> one_element_map;
  one_element_map.insert(123, "abc");
  Map<int32_t, std::string> three_element_map;
  three_element_map.insert(4, "d");
  three_element_map.insert(5, "e");
  three_element_map.insert(6, "f");

  std::ostringstream so;
  so << "null_map=" << null_map << ", empty_map=" << empty_map
     << ", one_element_map=" << one_element_map
     << ", three_element_map=" << three_element_map;

  EXPECT_EQ(
      "null_map=null, "
      "empty_map={}, "
      "one_element_map={123: abc}, "
      "three_element_map={4: d, 5: e, 6: f}",
      so.str());
}

TEST(FormattingTest, Structs) {
  InlinedStructPtr<Rect> inlined_struct_ptr = MakeRect(1);
  InlinedStructPtr<Rect> null_inlined_struct_ptr;
  StructPtr<RectPair> struct_ptr = RectPair::New();
  struct_ptr->first = MakeRect(2);
  StructPtr<RectPair> null_struct_ptr;

  std::ostringstream so;
  so << "inlined_struct_ptr=" << inlined_struct_ptr
     << ", null_inlined_struct_ptr=" << null_inlined_struct_ptr
     << ", struct_ptr=" << struct_ptr
     << ", null_struct_ptr=" << null_struct_ptr;
  EXPECT_EQ(
      "inlined_struct_ptr={x=1, y=2, width=10, height=20}, "
      "null_inlined_struct_ptr=null, "
      "struct_ptr={first={x=2, y=4, width=20, height=40}, second=null}, "
      "null_struct_ptr=null",
      so.str());
}

}  // namespace test
}  // namespace fidl
