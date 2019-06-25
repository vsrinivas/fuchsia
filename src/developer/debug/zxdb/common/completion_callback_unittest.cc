// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/completion_callback.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(CompletionCallback, NoParams) {
  // Lambda that records it was called and the error it was passed.
  bool called = false;
  Err out_err;
  auto set_out = [&out_err, &called](const Err& err) {
    called = true;
    out_err = err;
  };

  // Call with an error.
  CompletionCallback<> cb_with_err(set_out);
  CompletionCallback<> call_me(std::move(cb_with_err));  // Test move ctor.
  Err with_error("message");
  call_me(with_error);
  EXPECT_TRUE(called);
  EXPECT_EQ(with_error.msg(), out_err.msg());

  // Call with no error.
  called = false;
  CompletionCallback<> cb_no_err(set_out);
  call_me = std::move(cb_no_err);  // Test move operator.
  call_me();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
}

TEST(CompletionCallback, SomeParams) {
  // Lambda that records it was called and the error it was passed.
  bool called = false;
  Err out_err;
  int out_val = 99;  // Nonzero to catch default assignment to 0 below.
  auto set_out = [&out_err, &called, &out_val](const Err& err, int val) {
    called = true;
    out_err = err;
    out_val = val;
  };

  // Call with an error.
  CompletionCallback<int> cb_with_err(set_out);
  CompletionCallback<int> call_me(std::move(cb_with_err));  // Test move ctor.
  Err with_error("message");
  call_me(with_error);
  EXPECT_TRUE(called);
  EXPECT_EQ(with_error.msg(), out_err.msg());
  EXPECT_EQ(out_val, 0);

  // Call with no error.
  called = false;
  CompletionCallback<int> cb_no_err(set_out);
  call_me = std::move(cb_no_err);  // Test move operator.
  call_me(42);
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(42, out_val);

  // Call specifying everything.
  called = false;
  CompletionCallback<int> everything = set_out;  // Assignment from lambda.
  Err with_error2("other message");
  everything(with_error2, 1000);
  EXPECT_TRUE(called);
  EXPECT_EQ(with_error2.msg(), out_err.msg());
  EXPECT_EQ(out_val, 1000);
}

}  // namespace zxdb
