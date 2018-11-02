// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/functional/bind_callback.h"
#include "gtest/gtest.h"

namespace fxl {
namespace {

class SampleBinderClass {
 public:
  explicit SampleBinderClass(int& counter)
      : counter(counter), weak_ptr_factory_(this) {}

  void incrementBy(int by) { counter += by; }

  inline WeakPtr<SampleBinderClass> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  int getCounter() const { return counter; }

 private:
  int& counter;
  WeakPtrFactory<SampleBinderClass> weak_ptr_factory_;
};

TEST(BindCallback, UnwrapNoArgs) {
  int counter = 0;
  std::function<void()> cb;
  {
    SampleBinderClass dummy(counter);
    cb = BindWeakUnwrap(dummy.GetWeakPtr(), [](SampleBinderClass& sender) {
      sender.incrementBy(1);
    });
    EXPECT_EQ(counter, 0);
    cb();
    EXPECT_EQ(counter, 1);
    cb();
    EXPECT_EQ(counter, 2);
  }
  cb();  // should not increment
  EXPECT_EQ(counter, 2);
}

TEST(BindCallback, UnwrapArgs) {
  int counter = 0;
  std::function<void(int)> cb;
  {
    SampleBinderClass dummy(counter);
    cb = BindWeakUnwrap<int>(dummy.GetWeakPtr(),
                             [](SampleBinderClass& sender, int increment) {
                               sender.incrementBy(increment);
                             });
    EXPECT_EQ(counter, 0);
    cb(3);
    EXPECT_EQ(counter, 3);
    cb(7);
    EXPECT_EQ(counter, 10);
  }
  cb(1);  // should not increment
  EXPECT_EQ(counter, 10);
}

TEST(BindCallback, BindWeakSelf) {
  int counter = 0;
  std::function<void()> cb;
  {
    SampleBinderClass dummy(counter);
    cb = BindWeakSelf(dummy.GetWeakPtr(), &SampleBinderClass::incrementBy, 2);
    EXPECT_EQ(counter, 0);
    cb();
    EXPECT_EQ(counter, 2);
    cb();
    EXPECT_EQ(counter, 4);
  }
  cb();  // should not increment
  EXPECT_EQ(counter, 4);
}

TEST(BindCallback, BindWeakPlaceHolder) {
  int counter = 0;

  std::function<void(int)> cb;
  {
    SampleBinderClass dummy(counter);

    cb = BindWeak<int>(dummy.GetWeakPtr(),
                       std::bind(&SampleBinderClass::incrementBy, &dummy,
                                 std::placeholders::_1));
    EXPECT_EQ(counter, 0);
    cb(3);
    EXPECT_EQ(counter, 3);
    cb(7);
    EXPECT_EQ(counter, 10);
  }
  cb(1);  // should not increment
  EXPECT_EQ(counter, 10);
}

TEST(BindCallback, BindWeakMutable) {
  int counter = 0;
  int someInt = 3;
  SampleBinderClass dummy(counter);
  // make sure we support mutable lambdas to be bound
  auto cb =
      BindWeakUnwrap(dummy.GetWeakPtr(), [=](SampleBinderClass& cl) mutable {
        someInt = 4;
        cl.incrementBy(someInt);
      });
  auto cb2 = BindWeak(dummy.GetWeakPtr(), [someInt, &counter]() mutable {
    someInt = 6;
    counter += someInt;
  });
  cb();
  EXPECT_EQ(counter, 4);
  cb2();
  EXPECT_EQ(counter, 10);
}

}  // namespace
}  // namespace fxl
