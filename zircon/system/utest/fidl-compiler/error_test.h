// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_

#define ASSERT_COMPILED(library)                  \
  {                                               \
    TestLibrary& library_ref = (library);         \
    if (!library_ref.Compile()) {                 \
      const auto& errors = library_ref.errors();  \
      EXPECT_EQ(errors.size(), 0);                \
      for (const auto& error : errors) {          \
        EXPECT_STR_EQ("", error->err.msg.data()); \
      }                                           \
      FAIL("stopping test, compilation failed");  \
    }                                             \
  }

#define ASSERT_ERRORED_DURING_COMPILE(library, error) \
  {                                                   \
    TestLibrary& library_ref = (library);             \
    ASSERT_FALSE(library_ref.Compile());              \
    ASSERT_EQ(library_ref.errors().size(), 1u);       \
    EXPECT_ERR(library_ref.errors()[0], (error));     \
  }

#define ASSERT_ERRORED_DURING_COMPILE_WITH_DEP(library, dep, error) \
  {                                                                 \
    TestLibrary& library_ref = (library);                           \
    library_ref.AddDependentLibrary(std::move(dep));                \
    ASSERT_FALSE(library_ref.Compile());                            \
    ASSERT_EQ(library_ref.errors().size(), 1u);                     \
    EXPECT_ERR(library_ref.errors()[0], (error));                   \
  }

#define ASSERT_ERRORED_TWICE_DURING_COMPILE(library, err0, err1) \
  {                                                              \
    TestLibrary& library_ref = (library);                        \
    ASSERT_FALSE(library_ref.Compile());                         \
    ASSERT_EQ(library_ref.errors().size(), 2u);                  \
    EXPECT_ERR(library_ref.errors()[0], (err0));                 \
    EXPECT_ERR(library_ref.errors()[1], (err1));                 \
  }

#define ASSERT_ERR(actual_err, err_def, ...) \
  ASSERT_STR_EQ(actual_err->err.msg.data(), err_def.msg.data(), ##__VA_ARGS__)

#define EXPECT_ERR(actual_err, err_def, ...) \
  EXPECT_STR_EQ(actual_err->err.msg.data(), err_def.msg.data(), ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_
