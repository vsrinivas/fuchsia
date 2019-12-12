// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_struct.h"

#include <gtest/gtest.h>

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
      x = -1;
    }
  };

  ScopedStruct<Foo> bar;
  EXPECT_EQ(bar->x, 100);

  ScopedStruct<Foo> bar2(20);
  EXPECT_EQ(bar2->x, 20);

  ScopedStruct<Foo> bar3 = std::move(bar);
  EXPECT_EQ(bar->x, 0);
  EXPECT_EQ(bar2->x, 20);
  EXPECT_EQ(bar3->x, 100);
}

// Class with custom traits.

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
  }
  EXPECT_EQ(counters.init, 1);
  EXPECT_EQ(counters.destroy, 2);
  EXPECT_EQ(counters.move, 1);
}
