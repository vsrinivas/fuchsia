// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.error.methods/cpp/fidl.h>
#include <lib/fidl/cpp/any_error_in.h>

#include <zxtest/zxtest.h>

namespace {

// Test that the |AnyErrorIn| template is wired up correctly: it should contain
// the corresponding domain specific error.

// NoArgsPrimitiveError(struct { should_error bool; }) -> (struct {}) error int32;
static_assert(std::is_base_of<
              fidl::internal::AnyErrorInImpl<int32_t>,
              fidl::AnyErrorIn<test_error_methods::ErrorMethods::NoArgsPrimitiveError>>::value);

// ManyArgsCustomError(struct { should_error bool; })
//     -> (struct { a int32; b int32; c int32; }) error MyError;
static_assert(std::is_base_of<
              fidl::internal::AnyErrorInImpl<test_error_methods::MyError>,
              fidl::AnyErrorIn<test_error_methods::ErrorMethods::ManyArgsCustomError>>::value);

using ::test_error_methods::MyError;
using AnyErrorInMethod = fidl::AnyErrorIn<test_error_methods::ErrorMethods::ManyArgsCustomError>;

TEST(AnyErrorInMethod, TransportError) {
  AnyErrorInMethod error(fidl::Status::UnknownOrdinal());
  EXPECT_TRUE(error.is_transport_error());
  EXPECT_FALSE(error.is_application_error());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, error.transport_error().reason());
  EXPECT_EQ(
      "FIDL operation failed due to unexpected message, status: "
      "ZX_ERR_NOT_SUPPORTED (-2), detail: unknown ordinal",
      error.FormatDescription());
}

TEST(AnyErrorInMethod, ApplicationError) {
  AnyErrorInMethod error(MyError::kBadError);
  EXPECT_FALSE(error.is_transport_error());
  EXPECT_TRUE(error.is_application_error());
  EXPECT_EQ(MyError::kBadError, error.application_error());
  EXPECT_EQ("FIDL application error: test.error.methods/MyError.BAD_ERROR (value: 1)",
            error.FormatDescription());
}

TEST(AnyErrorInMethod, UnknownApplicationError) {
  AnyErrorInMethod error(static_cast<MyError>(42));
  EXPECT_FALSE(error.is_transport_error());
  EXPECT_TRUE(error.is_application_error());
  EXPECT_EQ("FIDL application error: test.error.methods/MyError.[UNKNOWN] (value: 42)",
            error.FormatDescription());
}

TEST(AnyErrorInMethod, SignedNumberedApplicationError) {
  fidl::internal::AnyErrorInImpl<int32_t> error(-3);
  EXPECT_FALSE(error.is_transport_error());
  EXPECT_TRUE(error.is_application_error());
  EXPECT_EQ(-3, error.application_error());
  EXPECT_EQ("FIDL application error: int32_t (value: -3)", error.FormatDescription());
}

TEST(AnyErrorInMethod, UnsignedNumberedApplicationError) {
  fidl::internal::AnyErrorInImpl<uint32_t> error(3);
  EXPECT_FALSE(error.is_transport_error());
  EXPECT_TRUE(error.is_application_error());
  EXPECT_EQ(3, error.application_error());
  EXPECT_EQ("FIDL application error: uint32_t (value: 3)", error.FormatDescription());
}

}  // namespace
