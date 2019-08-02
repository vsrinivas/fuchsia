// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
namespace fake {
namespace {

TEST(FakeObjectIdentifierFactoryImpl, LiveIsCorrect) {
  const ObjectDigest digest("some digest");
  const ObjectDigest another_digest("another digest");

  FakeObjectIdentifierFactory factory;
  EXPECT_FALSE(factory.IsLive(digest));
  EXPECT_FALSE(factory.IsLive(another_digest));

  auto identifier_1 = factory.MakeObjectIdentifier(0u, 0u, digest);
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_FALSE(factory.IsLive(another_digest));

  auto identifier_2 = factory.MakeObjectIdentifier(1u, 2u, digest);
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_FALSE(factory.IsLive(another_digest));

  auto identifier_3 = factory.MakeObjectIdentifier(0u, 0u, another_digest);
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  auto identifier_4 = identifier_3;
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  auto identifier_5 = std::move(identifier_4);
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  identifier_1 = ObjectIdentifier();
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  identifier_2 = ObjectIdentifier();
  EXPECT_FALSE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  // Re-creating an expired is working as expected.
  identifier_2 = factory.MakeObjectIdentifier(1u, 2u, digest);
  EXPECT_TRUE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  identifier_2 = ObjectIdentifier();
  EXPECT_FALSE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  identifier_3 = ObjectIdentifier();
  EXPECT_FALSE(factory.IsLive(digest));
  EXPECT_TRUE(factory.IsLive(another_digest));

  identifier_5 = ObjectIdentifier();
  EXPECT_FALSE(factory.IsLive(digest));
  EXPECT_FALSE(factory.IsLive(another_digest));
}

TEST(FakeObjectIdentifierFactoryImpl, IdentifierReturnsCorrectFactory) {
  ObjectIdentifier identifier;

  {
    FakeObjectIdentifierFactory factory;
    identifier = factory.MakeObjectIdentifier(0u, 0u, ObjectDigest(""));
    EXPECT_EQ(identifier.factory(), &factory);
  }
  // Factory expired.
  EXPECT_EQ(identifier.factory(), nullptr);
}

}  // namespace
}  // namespace fake
}  // namespace storage
