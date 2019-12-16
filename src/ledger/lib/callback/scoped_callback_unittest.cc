// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/scoped_callback.h"

#include "gtest/gtest.h"

namespace ledger {
namespace {

class Witness {
 public:
  explicit Witness(bool* value) : value_(value) {}

  explicit operator bool() const { return *value_; }

 private:
  bool* value_;
};

TEST(ScopedCallback, Witness) {
  bool witness_value = true;
  const Witness witness(&witness_value);
  bool called = false;
  auto call = [&called] { called = true; };

  // Witness is true at creation, true at execution.
  {
    witness_value = true;
    called = false;
    auto callback = MakeScoped(witness, call);
    witness_value = true;
    callback();
    EXPECT_TRUE(called);
  }

  // Witness is true at creation, false at execution.
  {
    witness_value = true;
    called = false;
    auto callback = MakeScoped(witness, call);
    witness_value = false;
    callback();
    EXPECT_FALSE(called);
  }

  // Witness is false at creation, true at execution.
  {
    witness_value = false;
    called = false;
    auto callback = MakeScoped(witness, call);
    witness_value = true;
    callback();
    EXPECT_TRUE(called);
  }

  // Witness is false at creation, false at execution.
  {
    witness_value = false;
    called = false;
    auto callback = MakeScoped(witness, call);
    witness_value = false;
    callback();
    EXPECT_FALSE(called);
  }
}
}  // namespace
}  // namespace ledger
