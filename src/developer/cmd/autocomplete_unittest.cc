// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/autocomplete.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Command, Parse) {
  {
    cmd::Autocomplete autocomplete("");
    EXPECT_TRUE(autocomplete.tokens().empty());
    EXPECT_EQ("", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("  \t  ");
    EXPECT_TRUE(autocomplete.tokens().empty());
    EXPECT_EQ("", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("# This is a comment");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("#", "This", "is", "a"));
    EXPECT_EQ("comment", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete(" #Also a comment ");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("#Also", "a", "comment"));
    EXPECT_EQ("", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls");
    EXPECT_TRUE(autocomplete.tokens().empty());
    EXPECT_EQ("ls", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls -lart");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls"));
    EXPECT_EQ("-lart", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls#not-a-comment");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre());
    EXPECT_EQ("ls#not-a-comment", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls #a-comment");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls"));
    EXPECT_EQ("#a-comment", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete(" ls \t -lart \n banana\r");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls", "-lart", "banana"));
    EXPECT_EQ("", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete(" \"\" ");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("\"\""));
    EXPECT_EQ("", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls \" \" -lart");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls", "\"", "\""));
    EXPECT_EQ("-lart", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("really ls\"not\" a-quote");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("really", "ls\"not\""));
    EXPECT_EQ("a-quote", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls \"parse-error");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls"));
    EXPECT_EQ("\"parse-error", autocomplete.fragment());
  }

  {
    cmd::Autocomplete autocomplete("ls \"also-parse-error  ");
    ASSERT_THAT(autocomplete.tokens(), testing::ElementsAre("ls", "\"also-parse-error"));
    EXPECT_EQ("", autocomplete.fragment());
  }
}

TEST(Command, AddCompletion) {
  cmd::Autocomplete autocomplete("ls /bin/l");
  autocomplete.AddCompletion("/bin/ls");
  autocomplete.AddCompletion("/bin/ln");
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::ElementsAre("ls /bin/ls", "ls /bin/ln"));
}

TEST(Command, CompleteAsPathAbsolute) {
  cmd::Autocomplete autocomplete("ls /pk");
  autocomplete.CompleteAsPath();
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::ElementsAre("ls /pkg"));
}

TEST(Command, CompleteAsPathRelative) {
  cmd::Autocomplete autocomplete("ls pk");
  autocomplete.CompleteAsPath();
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::ElementsAre("ls pkg"));
}

TEST(Command, CompleteEmptyAsPathRelative) {
  cmd::Autocomplete autocomplete("");
  autocomplete.CompleteAsPath();
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::Contains("pkg"));
}

TEST(Command, CompleteAsDirectoryEntry) {
  cmd::Autocomplete autocomplete("ls met");
  autocomplete.CompleteAsDirectoryEntry("/pkg");
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::ElementsAre("ls meta"));
}

TEST(Command, CompleteAsEnvironmentVariable) {
  setenv("MY_TEST_ENVIRON_VAR", "BANANA", 1);
  cmd::Autocomplete autocomplete("getenv MY_TEST_ENVIRON_VA");
  autocomplete.CompleteAsEnvironmentVariable();
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::ElementsAre("getenv MY_TEST_ENVIRON_VAR"));
  unsetenv("MY_TEST_ENVIRON_VAR");
}

TEST(Command, CompleteEmptyStringAsEnvironmentVariable) {
  setenv("AAAAA_MY_TEST_ENV", "BANANA", 1);
  cmd::Autocomplete autocomplete("");
  autocomplete.CompleteAsEnvironmentVariable();
  ASSERT_THAT(autocomplete.TakeCompletions(), testing::Contains("AAAAA_MY_TEST_ENV"));
  unsetenv("AAAAA_MY_TEST_ENV");
}

}  // namespace
