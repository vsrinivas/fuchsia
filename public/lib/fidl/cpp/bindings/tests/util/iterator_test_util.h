// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_ITERATOR_TEST_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_ITERATOR_TEST_UTIL_H_

#include <algorithm>
#include <vector>

#include "gtest/gtest.h"

namespace fidl {
namespace test {

template <typename Iterator, typename ValueType>
void ExpectIteratorValues(Iterator begin,
                          Iterator end,
                          const std::vector<ValueType>& expected_values) {
  size_t i = 0u;
  for (Iterator it = begin; it != end; ++it, ++i) {
    EXPECT_EQ(*it, expected_values[i]);
  }
  EXPECT_EQ(i, expected_values.size());
}

// This helper template checks that the specified `Iterator` satisfies the C++
// BidirectionalIterator concept, as described in:
//   http://en.cppreference.com/w/cpp/concept/BidirectionalIterator.
//
// This template will mutate the values in the iterator.
template <typename Iterator, typename ValueType>
void ExpectBidiIteratorConcept(Iterator begin,
                               Iterator end,
                               const std::vector<ValueType>& expected_values) {
  {
    // Default iterator is constructible.  Deferencing it is undefined
    // behaviour.
    Iterator default_iter;
  }

  // Now we test bidirecionality:
  // Prefix increment.
  Iterator iter1(begin);
  EXPECT_EQ(*iter1, expected_values[0]);

  Iterator iter2(begin);
  ++iter2;

  EXPECT_NE(iter1, iter2);
  EXPECT_EQ(*iter2, expected_values[1]);
  ++iter1;
  EXPECT_EQ(iter1, iter2);

  // Postfix increment.
  Iterator prev_iter2 = iter2++;  // element 2
  EXPECT_EQ(*prev_iter2, expected_values[1]);
  EXPECT_EQ(prev_iter2, iter1);
  EXPECT_EQ(*iter2, expected_values[2]);

  // Prefix decrement.
  EXPECT_EQ(--iter2, prev_iter2);  // element 1
  EXPECT_EQ(--iter1, begin);       // element 0
  // Postfix decrement.
  EXPECT_EQ(iter2--, prev_iter2);  // element 0

  // Equality.
  EXPECT_EQ(iter1, begin);
  EXPECT_NE(++iter2, begin);

  // Copy constructible and assignable.
  Iterator iter3 = iter2;
  EXPECT_EQ(iter3, iter2);
  EXPECT_EQ(*iter3, *iter2);

  Iterator iter3_cp(iter3);
  EXPECT_EQ(iter3_cp, iter3);
  EXPECT_EQ(*iter3_cp, *iter3);

  // Move constructible and assignable.
  Iterator iter3_mv = static_cast<Iterator&&>(iter3);
  Iterator iter3_cp_mv;
  iter3_cp_mv = static_cast<Iterator&&>(iter3_cp);
  EXPECT_EQ(iter3_mv, iter3_cp_mv);

  // operator->.
  EXPECT_EQ(*(iter3_mv.operator->()), expected_values[1]);

  // Swap two iterators:
  Iterator i1 = begin;
  Iterator i2 = ++begin;
  EXPECT_EQ(*i1, expected_values[0]);
  EXPECT_EQ(*i2, expected_values[1]);
  std::swap(i1, i2);
  EXPECT_EQ(*i1, expected_values[1]);
  EXPECT_EQ(*i2, expected_values[0]);
}

template <typename Iterator, typename ValueType>
void ExpectBidiMutableIteratorConcept(
    Iterator begin,
    Iterator end,
    const std::vector<ValueType>& expected_values) {
  Iterator it = begin;
  std::vector<ValueType> values = expected_values;

  {
    SCOPED_TRACE("Mutate 1st value.");
    *it = 12;
    values[0] = 12;
    ExpectIteratorValues(begin, end, values);
  }

  {
    SCOPED_TRACE("Mutate 2nd value.");
    *++it = 14;
    values[1] = 14;
    ExpectIteratorValues(begin, end, values);
  }

  {
    SCOPED_TRACE("Mutate 3rd value.");
    *--it = 22;
    values[0] = 22;
    ExpectIteratorValues(begin, end, values);
  }
}

}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TESTS_UTIL_ITERATOR_TEST_UTIL_H_
