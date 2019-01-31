// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/delaying_facade.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class Foo {
 public:
  void AddNumber(int i) { numbers.push_back(i); }

  void AddUniqueNumber(std::unique_ptr<int> i) {
    unique_numbers.push_back(std::move(i));
  }

  std::vector<int> numbers;
  std::vector<std::unique_ptr<int>> unique_numbers;
};

TEST(DelayingFacadeTest, CallOrder) {
  Foo foo;
  DelayingFacade<Foo> delayed_foo;

  // None of the operations should be executed before the object is set.
  delayed_foo.EnqueueCall(&Foo::AddNumber, 0);
  EXPECT_THAT(foo.numbers, IsEmpty());
  foo.AddNumber(1);
  EXPECT_THAT(foo.numbers, ElementsAre(1));

  delayed_foo.EnqueueCall(&Foo::AddNumber, 2);
  EXPECT_THAT(foo.numbers, ElementsAre(1));
  foo.AddNumber(3);
  EXPECT_THAT(foo.numbers, ElementsAre(1, 3));

  // Set foo as the object, and expect that the previous delayed operations are
  // executed.
  delayed_foo.SetTargetObject(&foo);
  EXPECT_THAT(foo.numbers, ElementsAre(1, 3, 0, 2));

  // All following operations on delayed_foo should be executed immediately.
  delayed_foo.EnqueueCall(&Foo::AddNumber, 4);
  EXPECT_THAT(foo.numbers, ElementsAre(1, 3, 0, 2, 4));
  foo.AddNumber(5);
  EXPECT_THAT(foo.numbers, ElementsAre(1, 3, 0, 2, 4, 5));
  delayed_foo.EnqueueCall(&Foo::AddNumber, 6);
  EXPECT_THAT(foo.numbers, ElementsAre(1, 3, 0, 2, 4, 5, 6));
}

TEST(DelayingFacadeTest, CallWithNotCopyableObjects) {
  Foo foo;
  DelayingFacade<Foo> delayed_foo;

  delayed_foo.EnqueueCall(&Foo::AddUniqueNumber, std::make_unique<int>(0));
  EXPECT_THAT(foo.unique_numbers, IsEmpty());

  delayed_foo.SetTargetObject(&foo);

  EXPECT_THAT(foo.unique_numbers, SizeIs(1));
  EXPECT_EQ(*foo.unique_numbers[0], 0);
}

}  // namespace
}  // namespace ledger
