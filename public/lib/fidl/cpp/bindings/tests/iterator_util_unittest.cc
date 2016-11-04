// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/iterator_util.h"
#include "lib/fidl/cpp/bindings/map.h"
#include "lib/fidl/cpp/bindings/tests/util/iterator_test_util.h"

namespace fidl {
namespace test {

using internal::MapKeyIterator;
using internal::MapValueIterator;

TEST(MapIteratorTest, KeyIterator) {
  Map<int, int> my_map;
  my_map[1] = 2;
  my_map[3] = 4;
  my_map[5] = 6;

  MapKeyIterator<int, int> key_iter(&my_map);
  std::vector<int> expected_vals;
  expected_vals.push_back(1);
  expected_vals.push_back(3);
  expected_vals.push_back(5);
  ExpectIteratorValues(key_iter.begin(), key_iter.end(), expected_vals);
}

TEST(MapIteratorTest, ValueIterator) {
  Map<int, int> my_map;
  my_map[1] = 2;
  my_map[3] = 4;
  my_map[5] = 6;

  MapValueIterator<int, int> value_iter(&my_map);
  std::vector<int> expected_vals;
  expected_vals.push_back(2);
  expected_vals.push_back(4);
  expected_vals.push_back(6);
  ExpectIteratorValues(value_iter.begin(), value_iter.end(), expected_vals);
}

TEST(MapIteratorTest, BidirectionalIteratorConcept) {
  Map<int, int> my_map;
  my_map[1] = 2;
  my_map[3] = 4;
  my_map[5] = 6;

  // Test common IteratorView specializations for Map keys, Map values, and
  // Arrays.
  MapKeyIterator<int, int> map_key_iter(&my_map);
  MapValueIterator<int, int> map_value_iter(&my_map);

  {
    SCOPED_TRACE("Test map key iterator bidirectionality.");
    std::vector<int> expected_vals;
    expected_vals.push_back(1);
    expected_vals.push_back(3);
    expected_vals.push_back(5);
    ExpectBidiIteratorConcept(map_key_iter.begin(), map_key_iter.end(),
                              expected_vals);
  }

  {
    SCOPED_TRACE("Test map value iterator bidirectionality.");
    std::vector<int> expected_vals;
    expected_vals.push_back(2);
    expected_vals.push_back(4);
    expected_vals.push_back(6);
    ExpectBidiIteratorConcept(map_value_iter.begin(), map_value_iter.end(),
                              expected_vals);
  }

  {
    SCOPED_TRACE("Test map value iterator mutability.");
    std::vector<int> expected_vals;
    expected_vals.push_back(2);
    expected_vals.push_back(4);
    expected_vals.push_back(6);
    ExpectBidiMutableIteratorConcept(map_value_iter.begin(),
                                     map_value_iter.end(), expected_vals);
  }
}

}  // namespace test
}  // namespace fidl
