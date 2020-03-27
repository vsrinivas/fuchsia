// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "function_example2.h"

#include <lib/fit/function.h>

// This example demonstrates using |fit::function| to capture a member
// function (add) and applying it to each element of a vector.
namespace function_example2 {

class accumulator {
 public:
  void add(int value) { sum += value; }

  int sum = 0;
};

void count_to_ten(fit::function<void(int)> fn) {
  for (int i = 1; i <= 10; i++) {
    fn(i);
  }
}

int sum_to_ten() {
  accumulator accum;
  count_to_ten(fit::bind_member(&accum, &accumulator::add));
  return accum.sum;
}

void run() { assert(sum_to_ten() == 55); }

}  // namespace function_example2
