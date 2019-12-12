// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "list_ostream.h"

#include <gtest/gtest.h>

#include <iostream>

TEST(list_ostream, NoComma)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  ls << "Hello"
     << "World!";
  EXPECT_EQ(ss.str(), "HelloWorld!");
}

TEST(list_ostream, SimpleList)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  ls << "Hello" << ls.comma << "World!";
  EXPECT_EQ(ss.str(), "Hello,World!");
}

TEST(list_ostream, SetComma)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  ls.set_comma(": ");
  ls << "Hello" << ls.comma << "World!";
  EXPECT_EQ(ss.str(), "Hello: World!");
}

TEST(list_ostream, IgnoreTrailingComma)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  ls << "Hello" << ls.comma << "World!" << ls.comma;
  EXPECT_EQ(ss.str(), "Hello,World!");
}

TEST(list_ostream, CompoundArguments)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  ls << "Hello"
     << "World!" << ls.comma << "Bonjour"
     << "Monde!" << ls.comma;
  EXPECT_EQ(ss.str(), "HelloWorld!,BonjourMonde!");
}
