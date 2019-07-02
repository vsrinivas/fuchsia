// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/util/collection_utils.h"

#include <gtest/gtest.h>

namespace {

using namespace scenic_impl::gfx;

class WeakValue {
 public:
  WeakValue(int value) : value_(value), weak_factory_(this) {}
  int value() const { return value_; }
  fxl::WeakPtr<WeakValue> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  int value_;
  fxl::WeakPtrFactory<WeakValue> weak_factory_;  // must be last
};

}  // anonymous namespace

TEST(CollectionUtils, ApplyToCompactedVector) {
  auto weak1 = std::make_unique<WeakValue>(1);
  auto weak2 = std::make_unique<WeakValue>(2);
  auto weak3 = std::make_unique<WeakValue>(3);
  auto weak4 = std::make_unique<WeakValue>(4);
  auto weak5 = std::make_unique<WeakValue>(5);
  auto weak6 = std::make_unique<WeakValue>(6);

  std::vector<fxl::WeakPtr<WeakValue>> values{
      weak1->GetWeakPtr(), weak2->GetWeakPtr(), weak3->GetWeakPtr(),
      weak4->GetWeakPtr(), weak5->GetWeakPtr(), weak6->GetWeakPtr(),
  };

  int sum = 0;
  auto AddToSum = [&sum](WeakValue* val) { sum += val->value(); };

  ApplyToCompactedVector(&values, AddToSum);
  EXPECT_EQ(sum, 21);
  EXPECT_EQ(values.size(), 6U);

  // Delete the third value; the sum should be reduced by 3 and the size of the
  // vector by 1.
  sum = 0;
  weak3.reset();
  ApplyToCompactedVector(&values, AddToSum);
  EXPECT_EQ(sum, 18);
  EXPECT_EQ(values.size(), 5U);

  // Reapply the closure; the result and vector size should remain unchanged.
  sum = 0;
  ApplyToCompactedVector(&values, AddToSum);
  EXPECT_EQ(sum, 18);
  EXPECT_EQ(values.size(), 5U);

  // Delete multiple values, including the first and last ones.
  sum = 0;
  weak1.reset();
  weak4.reset();
  weak6.reset();
  ApplyToCompactedVector(&values, AddToSum);
  EXPECT_EQ(sum, 7);
  EXPECT_EQ(values.size(), 2U);
}
