/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

extern "C" {
#include "sparse_array.h"
}

#include "gtest/gtest.h"

TEST(SparseArray, BasicFunctionality) {
  constexpr size_t numElems = 10;

  sparse_array_t sa;
  void* values[numElems] = {};

  sa_init(&sa, numElems);

  // Verify that all numbers [0..numElems) are used
  for (size_t ndx = 0; ndx < numElems; ndx++) {
    ssize_t result;
    void* fakeData = &values[ndx];
    result = sa_add(sa, fakeData);
    ASSERT_GE(result, 0);
    ASSERT_LT(result, (ssize_t)numElems);
    EXPECT_EQ(values[result], nullptr);
    values[result] = fakeData;
  }

  // An attempt to allocate one more should fail
  EXPECT_EQ(sa_add(sa, &values), -1);

  // Verify that we can get our values back
  for (ssize_t ndx = 0; ndx < (ssize_t)numElems; ndx++) {
    EXPECT_EQ(sa_get(sa, ndx), values[ndx]);
    ssize_t alt_ndx = numElems - (ndx + 1);
    EXPECT_EQ(sa_get(sa, alt_ndx), values[alt_ndx]);
  }

  // Clear out sparse array
  for (ssize_t ndx = 0; ndx < (ssize_t)numElems; ndx++) {
    sa_remove(sa, ndx);
  }

  // Verify that values have been cleared
  for (ssize_t ndx = 0; ndx < (ssize_t)numElems; ndx++) {
    EXPECT_EQ(sa_get(sa, ndx), nullptr);
  }

  sa_free(sa);
}

// For the churn test below, we'll maintain a reverse-indexed array. Specifically,
// each location in the array will contain a 'hasMyAddr' field that indicates which
// index in our sparse array contains a pointer to this location (or -1 if the
// location is not in our sparse array). Yes, it can be a bit confusing, but it
// simplifies the logic quite a bit. We will churn through locations in the array
// in a predefined pattern, and if each location is already in an index we will
// remove it from the sparse array. If it isn't already in the sparse array, we
// will add it and make note of which index contains that address.

struct tracking_data {
  bool seen;  // Used to keep track of which locations were seen during a for_each operation
  ssize_t hasMyAddr;
};

// Helper for use with for_each testing.
void note_uses(ssize_t ndx, void* ptr, void* ctx) {
  struct tracking_data* td = (struct tracking_data*)ptr;
  sparse_array_t sa = (sparse_array_t)ctx;

  ASSERT_NE(td, nullptr);

  // Keep track of which locations we've already seen
  EXPECT_EQ(td->seen, false);
  td->seen = true;

  // Make sure that the index in our location matches the index we were given
  ssize_t refNdx = td->hasMyAddr;
  ASSERT_NE(refNdx, -1);
  EXPECT_EQ(refNdx, ndx);

  // And that the sa_get gives us consisten info
  void* contents = sa_get(sa, refNdx);
  EXPECT_EQ(contents, (void*)td);
}

TEST(SparseArray, Churn) {
  constexpr size_t arraySize = 50;
  constexpr int steps[] = {1, 2, 3, 5, 7, 11, 13, 17, 19};
  constexpr size_t num_steps = sizeof(steps) / sizeof(int);
  constexpr size_t iterations = 111;

  // Each location will hold -1, or the index in which it is held in the sparse array
  struct {
    bool seen;
    ssize_t hasMyAddr;
  } values[arraySize];
  for (size_t ndx = 0; ndx < arraySize; ndx++) {
    values[ndx].hasMyAddr = -1;
  }

  sparse_array_t sa;

  sa_init(&sa, arraySize);

  // These two nested loops are simply intended to generate a semi-arbitrary pattern
  // through the array. Not random, but better than just always incrementing by 1.
  for (size_t step_ndx = 0; step_ndx < num_steps; step_ndx++) {
    int step = steps[step_ndx];
    size_t loc = 0;
    for (size_t iter = 0; iter < iterations; iter++) {
      if (values[loc].hasMyAddr == -1) {
        // Current location's address isn't in the sparse array, add it
        ssize_t sa_ndx = sa_add(sa, &values[loc]);
        EXPECT_NE(sa_ndx, -1);
        values[loc].hasMyAddr = sa_ndx;
      } else {
        // Current location's address is already in the sparse array, remove it
        EXPECT_EQ(sa_get(sa, values[loc].hasMyAddr), &values[loc]);
        sa_remove(sa, values[loc].hasMyAddr);
        values[loc].hasMyAddr = -1;
      }
      loc = (loc + step) % arraySize;
    }

    // Check that all used locations are returned by a sa_for_each operation
    for (size_t ndx = 0; ndx < arraySize; ndx++) {
      values[ndx].seen = false;
    }
    sa_for_each(sa, note_uses, sa);
    for (size_t ndx = 0; ndx < arraySize; ndx++) {
      EXPECT_EQ(values[ndx].seen, values[ndx].hasMyAddr != -1);
    }
  }

  sa_free(sa);
}
