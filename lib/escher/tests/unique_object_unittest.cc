// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <set>

#include "escher/gl/unique_object.h"

namespace {

int g_counter;
std::set<GLuint> g_ids;

void ResetTestObject(const GLuint id) {
  g_counter++;
  g_ids.insert(id);
}

}

TEST(UniqueObject, ResetAndDestroy) {
  g_counter = 0;
  g_ids.clear();

  {
    escher::UniqueObject<ResetTestObject> obj;
    EXPECT_EQ(0, obj.id());

    obj.Reset(1);
    EXPECT_EQ(0, g_counter);  // id was zero, so deleter was not called.

    obj.Reset(2);
    obj.Reset(3);
    EXPECT_EQ(3, obj.id());
    EXPECT_EQ(2, g_counter);
    EXPECT_EQ(2, g_ids.size());
    EXPECT_EQ(1, g_ids.count(1));
    EXPECT_EQ(1, g_ids.count(2));
    EXPECT_EQ(0, g_ids.count(3));

    obj.Reset(0);
    EXPECT_EQ(3, g_counter);
    EXPECT_EQ(3, g_ids.size());
    EXPECT_EQ(1, g_ids.count(3));

    obj.Reset(4);
    EXPECT_EQ(3, g_counter);  // id was zero, so deleter was not called.
  }
  EXPECT_EQ(4, g_counter);  // deleter was called by destructor.
  EXPECT_EQ(1, g_ids.count(4));
}
