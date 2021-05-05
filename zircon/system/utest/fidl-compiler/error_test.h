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

#define ASSERT_COMPILED_AND_CONVERT_WITH_DEP_INTO(library, dep, into) \
  {                                                                   \
    TestLibrary& library_ref = (library);                             \
    if (!library_ref.CompileAndCheckConversion(&(into), &(dep))) {    \
      const auto& errors = library_ref.errors();                      \
      EXPECT_EQ(errors.size(), 0);                                    \
      for (const auto& error : errors) {                              \
        EXPECT_STR_EQ("", error->err.msg.data());                     \
      }                                                               \
      FAIL("stopping test, compilation and conversion failed");       \
    }                                                                 \
  }

// ASSERT_COMPILED_AND_CONVERT_INTO takes an uninitialized TestLibrary
// and populates it with the result of compiling a converted file. This is
// useful for converting a library that will be consumed as a dependency of
// another library.
#define ASSERT_COMPILED_AND_CONVERT_INTO(library, into)                   \
  {                                                                       \
    TestLibrary no_dep;                                                   \
    TestLibrary& into_ref = (into);                                       \
    ASSERT_COMPILED_AND_CONVERT_WITH_DEP_INTO(library, no_dep, into_ref); \
  }

// ASSERT_COMPILED_AND_CONVERT_WITH_DEP allows a library to be converted
// with a dependency, generated from one of ASSERT_COMPILED_AND_CONVERT_INTO or
// ASSERT_COMPILED_AND_CLONE_INTO.
#define ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, dep)                \
  {                                                                       \
    TestLibrary no_into;                                                  \
    TestLibrary& dep_ref = (dep);                                         \
    ASSERT_COMPILED_AND_CONVERT_WITH_DEP_INTO(library, dep_ref, no_into); \
  }

#define ASSERT_COMPILED_AND_CONVERT(library)               \
  {                                                        \
    TestLibrary no_dep;                                    \
    ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, no_dep); \
  }

// ASSERT_COMPILED_AND_CLONE_INTO is identical to
// ASSERT_COMPILED_AND_CONVERT_INTO, except that it does not convert the second
// library (ie, it clones it instead). This is necessary because we will need
// two copies of the dependent library: one to successfully complete the
// pre-conversion compilation of the target library, and one to use as an
// unconverted dependency for its converted version.
#define ASSERT_COMPILED_AND_CLONE_INTO(library, into)                               \
  {                                                                                 \
    TestLibrary& library_ref = (library);                                           \
    TestLibrary no_dep;                                                             \
    if (!library_ref.CompileTwice(&(into), &(no_dep), fidl::utils::Syntax::kOld)) { \
      FAIL("stopping test, dependency duplication failed");                         \
    }                                                                               \
  }

#define ASSERT_ERR(actual_err, err_def, ...) \
  ASSERT_STR_EQ(actual_err->err.msg.data(), err_def.msg.data(), ##__VA_ARGS__)

#define EXPECT_ERR(actual_err, err_def, ...) \
  EXPECT_STR_EQ(actual_err->err.msg.data(), err_def.msg.data(), ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_ERROR_TEST_H_
