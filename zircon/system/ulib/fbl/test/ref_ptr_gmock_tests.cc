// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

class RefCountedInt : public fbl::RefCounted<RefCountedInt> {
 public:
  int x() const { return x_; }
  void set_x(int x) { x_ = x; }

 private:
  int x_ = 0;
};

TEST(RefPtrGmockTest, PointeeProperty) {
  fbl::RefPtr<RefCountedInt> int_ptr = fbl::MakeRefCounted<RefCountedInt>();
  int_ptr->set_x(1);
  EXPECT_THAT(int_ptr, testing::Pointee(testing::Property(&RefCountedInt::x, testing::Eq(1))));
}

}  // namespace
