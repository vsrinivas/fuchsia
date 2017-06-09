// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/cobalt_client/src/hello.pb.h"
#include "gtest/gtest.h"

TEST(HelloProto, Hello) {
  test::Person person;
  person.set_name("Fred");
  EXPECT_EQ("Fred", person.name());
}
