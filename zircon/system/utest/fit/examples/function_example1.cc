// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "function_example1.h"

#include <vector>

#include <lib/fit/function.h>

// This example demonstrates using fit::function to implement a higher order
// function called a left-fold.  |fold()| recursively combines elements in a
// vector.
namespace function_example1 {

using fold_function = fit::function<int(int value, int item)>;

int fold(const std::vector<int>& in, int value, const fold_function& f) {
  for (auto& item : in) {
    value = f(value, item);
  }
  return value;
}

int sum_item(int value, int item) { return value + item; }

int sum(const std::vector<int>& in) {
  // bind to a function pointer
  fold_function fn(&sum_item);
  return fold(in, 0, fn);
}

int alternating_sum(const std::vector<int>& in) {
  // bind to a lambda
  int sign = 1;
  fold_function fn([&sign](int value, int item) {
    value += sign * item;
    sign *= -1;
    return value;
  });
  return fold(in, 0, fn);
}

void run() {
  std::vector<int> in;
  for (int i = 0; i < 10; i++) {
    in.push_back(i);
  }
  assert(sum(in) == 45);
  assert(alternating_sum(in) == -5);
}

}  // namespace function_example1
