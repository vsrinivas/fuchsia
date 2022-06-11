// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_TESTS_ERROR_TEST_H_
#define TOOLS_FIDL_FIDLC_TESTS_ERROR_TEST_H_

#define ASSERT_COMPILED(library)                         \
  {                                                      \
    TestLibrary& library_ref = (library);                \
    if (!library_ref.Compile()) {                        \
      EXPECT_EQ(library_ref.errors().size(), 0);         \
      for (const auto& error : library_ref.errors()) {   \
        EXPECT_STREQ("", error->def.msg.data());         \
      }                                                  \
      FAIL("stopping test, compilation failed");         \
    }                                                    \
    EXPECT_EQ(library_ref.warnings().size(), 0);         \
    for (const auto& warning : library_ref.warnings()) { \
      EXPECT_STREQ("", warning->def.msg.data());         \
    }                                                    \
  }

#define ASSERT_WARNED_DURING_COMPILE(library, warn)    \
  {                                                    \
    TestLibrary& library_ref = (library);              \
    if (!library_ref.Compile()) {                      \
      EXPECT_EQ(library_ref.errors().size(), 0);       \
      for (const auto& error : library_ref.errors()) { \
        EXPECT_STREQ("", error->def.msg.data());       \
      }                                                \
      FAIL("stopping test, compilation failed");       \
    }                                                  \
    ASSERT_EQ(library_ref.warnings().size(), 1);       \
    EXPECT_ERR(library_ref.warnings()[0], (warn));     \
  }

#define ASSERT_WARNED_TWICE_DURING_COMPILE(library, warn0, warn1) \
  {                                                               \
    TestLibrary& library_ref = (library);                         \
    if (!library_ref.Compile()) {                                 \
      EXPECT_EQ(library_ref.errors().size(), 0);                  \
      for (const auto& error : library_ref.errors()) {            \
        EXPECT_STREQ("", error->def.msg.data());                  \
      }                                                           \
      FAIL("stopping test, compilation failed");                  \
    }                                                             \
    ASSERT_EQ(library_ref.warnings().size(), 2);                  \
    EXPECT_ERR(library_ref.warnings()[0], (warn0));               \
    EXPECT_ERR(library_ref.warnings()[1], (warn1));               \
  }

#define ASSERT_ERRORED_DURING_COMPILE(library, error)    \
  {                                                      \
    TestLibrary& library_ref = (library);                \
    ASSERT_FALSE(library_ref.Compile());                 \
    ASSERT_EQ(library_ref.errors().size(), 1u);          \
    EXPECT_ERR(library_ref.errors()[0], (error));        \
    EXPECT_EQ(library_ref.warnings().size(), 0);         \
    for (const auto& warning : library_ref.warnings()) { \
      EXPECT_STREQ("", warning->def.msg.data());         \
    }                                                    \
  }

#define ASSERT_ERRORED_TWICE_DURING_COMPILE(library, err0, err1) \
  {                                                              \
    TestLibrary& library_ref = (library);                        \
    ASSERT_FALSE(library_ref.Compile());                         \
    ASSERT_EQ(library_ref.errors().size(), 2u);                  \
    EXPECT_ERR(library_ref.errors()[0], (err0));                 \
    EXPECT_ERR(library_ref.errors()[1], (err1));                 \
    EXPECT_EQ(library_ref.warnings().size(), 0);                 \
    for (const auto& warning : library_ref.warnings()) {         \
      EXPECT_STREQ("", warning->def.msg.data());                 \
    }                                                            \
  }

#define ASSERT_ERR(actual_err, err_def, ...)                                     \
  {                                                                              \
    ASSERT_STREQ(actual_err->def.msg.data(), err_def.msg.data(), ##__VA_ARGS__); \
    ASSERT_TRUE(actual_err->span.valid());                                       \
  }

#define EXPECT_ERR(actual_err, err_def, ...)                                     \
  {                                                                              \
    EXPECT_STREQ(actual_err->def.msg.data(), err_def.msg.data(), ##__VA_ARGS__); \
    EXPECT_TRUE(actual_err->span.valid());                                       \
  }

#endif  // TOOLS_FIDL_FIDLC_TESTS_ERROR_TEST_H_
