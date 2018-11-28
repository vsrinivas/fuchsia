// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"
#include <memory>
#include "gtest/gtest.h"

namespace overnet {
namespace status_test {

typedef std::shared_ptr<int> BoxedInt;
BoxedInt boxed_int() { return BoxedInt(new int(42)); }

TEST(Status, Ok) {
  Status ok = Status::Ok();
  EXPECT_TRUE(ok.is_ok());
  EXPECT_EQ(StatusCode::OK, ok.code());
  EXPECT_EQ("", ok.reason());

  Status also_ok = ok;
  EXPECT_TRUE(also_ok.is_ok());
  EXPECT_EQ(StatusCode::OK, also_ok.code());
  EXPECT_EQ("", also_ok.reason());

  EXPECT_TRUE(ok.is_ok());
  EXPECT_EQ(StatusCode::OK, ok.code());
  EXPECT_EQ("", ok.reason());
}

TEST(Status, Cancelled) {
  Status cancelled = Status::Cancelled();
  EXPECT_TRUE(cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, cancelled.code());
  EXPECT_EQ("", cancelled.reason());

  Status also_cancelled = cancelled;
  EXPECT_TRUE(also_cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, also_cancelled.code());
  EXPECT_EQ("", also_cancelled.reason());

  EXPECT_TRUE(cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, cancelled.code());
  EXPECT_EQ("", cancelled.reason());
}

TEST(Status, Arbitrary) {
  Status blah(StatusCode::DATA_LOSS, "The bits got old");
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());

  Status copied = blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, copied.code());
  EXPECT_EQ("The bits got old", copied.reason());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());

  Status other_blah = Status(StatusCode::FAILED_PRECONDITION, "Something");
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());

  copied = other_blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, copied.code());
  EXPECT_EQ("Something", copied.reason());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());

  copied = Status::Ok();
  EXPECT_FALSE(copied.is_error());
  EXPECT_TRUE(copied.is_ok());
  EXPECT_EQ(StatusCode::OK, copied.code());
  EXPECT_EQ("", copied.reason());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());

  copied = other_blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, copied.code());
  EXPECT_EQ("Something", copied.reason());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());
}

TEST(StatusOr, Ok) {
  StatusOr<BoxedInt> ok(boxed_int());
  EXPECT_TRUE(ok.is_ok());
  EXPECT_EQ(StatusCode::OK, ok.code());
  EXPECT_EQ("", ok.reason());
  EXPECT_EQ(42, **ok.get());

  auto also_ok = ok;
  EXPECT_TRUE(also_ok.is_ok());
  EXPECT_EQ(StatusCode::OK, also_ok.code());
  EXPECT_EQ("", also_ok.reason());
  EXPECT_EQ(42, **also_ok.get());

  EXPECT_TRUE(ok.is_ok());
  EXPECT_EQ(StatusCode::OK, ok.code());
  EXPECT_EQ("", ok.reason());
  EXPECT_EQ(42, **ok.get());
}

TEST(StatusOr, Cancelled) {
  StatusOr<BoxedInt> cancelled(StatusCode::CANCELLED, "Cancelled");
  EXPECT_TRUE(cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, cancelled.code());
  EXPECT_EQ("Cancelled", cancelled.reason());
  EXPECT_EQ(nullptr, cancelled.get());

  auto also_cancelled = cancelled;
  EXPECT_TRUE(also_cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, also_cancelled.code());
  EXPECT_EQ("Cancelled", also_cancelled.reason());
  EXPECT_EQ(nullptr, cancelled.get());

  EXPECT_TRUE(cancelled.is_error());
  EXPECT_EQ(StatusCode::CANCELLED, cancelled.code());
  EXPECT_EQ("Cancelled", cancelled.reason());
  EXPECT_EQ(nullptr, cancelled.get());
}

TEST(StatusOr, Arbitrary) {
  StatusOr<BoxedInt> blah(StatusCode::DATA_LOSS, "The bits got old");
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_EQ(nullptr, blah.get());

  StatusOr<BoxedInt> copied = blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, copied.code());
  EXPECT_EQ("The bits got old", copied.reason());
  EXPECT_EQ(nullptr, copied.get());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_EQ(nullptr, blah.get());

  StatusOr<BoxedInt> other_blah =
      StatusOr<BoxedInt>(StatusCode::FAILED_PRECONDITION, "Something");
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());
  EXPECT_EQ(nullptr, other_blah.get());

  copied = other_blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, copied.code());
  EXPECT_EQ("Something", copied.reason());
  EXPECT_EQ(nullptr, copied.get());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_EQ(nullptr, blah.get());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());
  EXPECT_EQ(nullptr, other_blah.get());

  copied = StatusOr<BoxedInt>(boxed_int());
  EXPECT_FALSE(copied.is_error());
  EXPECT_TRUE(copied.is_ok());
  EXPECT_EQ(StatusCode::OK, copied.code());
  EXPECT_EQ("", copied.reason());
  EXPECT_EQ(42, **copied.get());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_EQ(nullptr, blah.get());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());
  EXPECT_EQ(nullptr, other_blah.get());

  copied = other_blah;
  EXPECT_TRUE(copied.is_error());
  EXPECT_FALSE(copied.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, copied.code());
  EXPECT_EQ("Something", copied.reason());
  EXPECT_EQ(nullptr, copied.get());
  EXPECT_TRUE(blah.is_error());
  EXPECT_FALSE(blah.is_ok());
  EXPECT_EQ(StatusCode::DATA_LOSS, blah.code());
  EXPECT_EQ("The bits got old", blah.reason());
  EXPECT_EQ(nullptr, blah.get());
  EXPECT_TRUE(other_blah.is_error());
  EXPECT_FALSE(other_blah.is_ok());
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, other_blah.code());
  EXPECT_EQ("Something", other_blah.reason());
  EXPECT_EQ(nullptr, other_blah.get());
}

}  // namespace status_test
}  // namespace overnet
