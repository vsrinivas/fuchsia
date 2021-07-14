// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the C++ language binding for the underlying rust code.

#include "lookup.h"

#include <lib/fpromise/result.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace intl {
namespace testing {
namespace {

// The usual lookup operations are overridden here with fake ones.
const intl_lookup_ops_t OPS = intl_lookup_ops_t{
    .op_new = intl_lookup_new_fake_for_test,
    .op_delete = intl_lookup_delete_fake_for_test,
    .op_string = intl_lookup_string_fake_for_test,
};

TEST(Intl, CreateErrorWithoutSeparateOps) {
  std::vector<std::string> locale_ids = {"en-US"};
  auto result = Lookup::NewForTest(locale_ids);
  EXPECT_TRUE(result.is_error());
}

TEST(Intl, CreateError) {
  std::vector<std::string> locale_ids = {"en-US"};
  auto result = Lookup::NewForTest(locale_ids, OPS);
  EXPECT_TRUE(result.is_error());
}

TEST(Intl, LookupReturnValues) {
  std::vector<std::string> locale_ids = {"nl-NL"};
  auto result = Lookup::NewForTest(locale_ids, OPS);
  EXPECT_TRUE(result.is_ok()) << "expected no error, was: " << static_cast<uint8_t>(result.error());
  std::unique_ptr<Lookup> lookup = result.take_value();

  EXPECT_FALSE(lookup->String(10).is_error());
  EXPECT_TRUE(lookup->String(11).is_error());
}

}  // namespace
}  // namespace testing
}  // namespace intl
