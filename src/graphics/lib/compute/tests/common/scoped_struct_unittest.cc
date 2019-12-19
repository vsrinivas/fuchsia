// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_struct.h"

#include <gtest/gtest.h>

#include <vector>

// Default traits.

TEST(ScopedStruct, DefaultTraits)
{
  struct Foo
  {
    int x = 0;

    void
    init()
    {
      x = 100;
    }
    void
    init(int xx)
    {
      x = xx;
    }
    void
    reset()
    {
      // NOTE: Normally, reset() should restore the default value (0 here), but
      // for the purpose of this test, use something else to verify if the function
      // was called.
      x = -1;
    }
  };

  ScopedStruct<Foo> bar;
  EXPECT_EQ(bar->x, 100) << "Invalid default value, init() was not called!";

  ScopedStruct<Foo> bar2(20);
  EXPECT_EQ(bar2->x, 20) << "Invalid initial value, init(int) was not called!";

  ScopedStruct<Foo> bar3 = std::move(bar);
  EXPECT_EQ(bar->x, 0) << "Invalid move-src value " << bar->x
                       << " reset() should not be called during move!";
  EXPECT_EQ(bar2->x, 20);
  EXPECT_EQ(bar3->x, 100) << "Invalid move-dst value!";

  Foo               foo42{ 42 };
  ScopedStruct<Foo> bar4 = makeScopedStruct(std::move(foo42));
  EXPECT_EQ(bar4->x, 42) << "makeScopedStruct() didn't set dst value properly";
  EXPECT_EQ(foo42.x, 0) << "makeScopedStruct() didn't move src value properly";

  std::vector<ScopedStruct<Foo>> scoped_foos;
  scoped_foos.push_back(makeScopedStruct(Foo{ 10 }));
  scoped_foos.push_back(ScopedStruct<Foo>(20));
  scoped_foos.emplace_back(30);
  EXPECT_EQ(scoped_foos.size(), 3u);
  EXPECT_EQ(scoped_foos[0]->x, 10);
  EXPECT_EQ(scoped_foos[1]->x, 20);
  EXPECT_EQ(scoped_foos[2]->x, 30);
}

// Class with custom traits.
// This allows counting the times init(), destroy() and move() are called.
//

static struct
{
  int init    = 0;
  int destroy = 0;
  int move    = 0;

  void
  clear()
  {
    init    = 0;
    destroy = 0;
    move    = 0;
  }
} counters;

struct Foo
{
  int x = 0;
};

struct FooTraits
{
  static constexpr Foo kDefault = {};

  static void
  init(Foo * obj)
  {
    obj->x = 42;
    counters.init++;
  }
  static void
  init(Foo * obj, int value)
  {
    obj->x = value;
    counters.init++;
  }
  static void
  destroy(Foo * obj)
  {
    obj->x = -1;
    counters.destroy++;
  }
  static void
  move(Foo * obj, Foo * from)
  {
    obj->x  = from->x;
    from->x = -2;
    counters.move++;
  }
};

TEST(ScopedStruct, CustomTraits)
{
  {
    ScopedStruct<Foo, FooTraits> foo;
    EXPECT_EQ(foo->x, 42);
    EXPECT_EQ(counters.init, 1);
    EXPECT_EQ(counters.destroy, 0);
    EXPECT_EQ(counters.move, 0);

    ScopedStruct<Foo, FooTraits> foo2(std::move(foo));
    EXPECT_EQ(foo2->x, 42);
    EXPECT_EQ(foo->x, -2);
    EXPECT_EQ(counters.init, 1);
    EXPECT_EQ(counters.destroy, 0);
    EXPECT_EQ(counters.move, 1);

    ScopedStruct<Foo, FooTraits> foo3(30);
    EXPECT_EQ(foo3->x, 30);
    EXPECT_EQ(counters.init, 2);
    EXPECT_EQ(counters.destroy, 0);
    EXPECT_EQ(counters.move, 1);
  }
  EXPECT_EQ(counters.init, 2);
  EXPECT_EQ(counters.destroy, 3);
  EXPECT_EQ(counters.move, 1);

  std::vector<ScopedStruct<Foo, FooTraits>> foos;

  // Reserve room to ensure that push_back() only performs single moves and
  // never tries to reallocate the underlying array and move items from the
  // old to the new storage locations, which is implementation dependent.
  foos.reserve(10);

  counters.init    = 0;
  counters.destroy = 0;
  counters.move    = 0;

  foos.push_back(makeScopedStruct<Foo, FooTraits>(Foo{ 10 }));

  // makeScopedStruct() creates an instance (calls init())
  // then moves the data into it (one move())
  // then push_back() moves the instance into the vector (one move() + one destroy()).
  EXPECT_EQ(counters.init, 1);
  EXPECT_EQ(counters.destroy, 1);
  EXPECT_EQ(counters.move, 2);

  // Builds an instance and moves it into the vector.
  // one init() + one move() + one destroy().
  foos.push_back(ScopedStruct<Foo, FooTraits>(20));
  EXPECT_EQ(counters.init, 2);
  EXPECT_EQ(counters.destroy, 2);
  EXPECT_EQ(counters.move, 3);

  // This should build the instance in-place, so only one init().
  foos.emplace_back(30);
  EXPECT_EQ(counters.init, 3);
  EXPECT_EQ(counters.destroy, 2);
  EXPECT_EQ(counters.move, 3);

  EXPECT_EQ(foos.size(), 3u);

  // Destroys three instances.
  foos.clear();
  EXPECT_EQ(counters.init, 3);
  EXPECT_EQ(counters.destroy, 5);
  EXPECT_EQ(counters.move, 3);
}
