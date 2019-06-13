// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {
namespace fake {
namespace {

TEST(FakeObjectTest, FakeObjectToken) {
  const ObjectIdentifier identifier(1u, 2u, ObjectDigest("some digest"));
  auto token = std::make_unique<FakeObjectToken>(identifier);
  EXPECT_EQ(token->GetIdentifier(), identifier);

  FakeTokenChecker checker = token->GetChecker();
  EXPECT_TRUE(checker);
  token.reset();
  EXPECT_FALSE(checker);
}

}  // namespace
}  // namespace fake
}  // namespace storage
