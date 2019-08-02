// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using ObjectIdentifierFactoryImplTest = ledger::TestWithEnvironment;

TEST_F(ObjectIdentifierFactoryImplTest, CountsAndCleansUp) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  const ObjectDigest another_digest = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory;
  EXPECT_EQ(factory.count(digest), 0);
  EXPECT_EQ(factory.size(), 0);

  auto identifier_1 = factory.MakeObjectIdentifier(0u, 0u, digest);
  EXPECT_EQ(factory.count(digest), 1);
  EXPECT_EQ(factory.size(), 1);

  // Tracking is per-digest, not per-identifier.
  auto identifier_2 = factory.MakeObjectIdentifier(1u, 2u, digest);
  EXPECT_EQ(factory.count(digest), 2);
  EXPECT_EQ(factory.size(), 1);

  // Distinct digests are tracked separately.
  auto identifier_3 = factory.MakeObjectIdentifier(0u, 0u, another_digest);
  EXPECT_EQ(factory.count(digest), 2);
  EXPECT_EQ(factory.count(another_digest), 1);
  EXPECT_EQ(factory.size(), 2);

  // Identifiers are tracked across copies.
  auto identifier_4 = identifier_3;
  EXPECT_EQ(factory.count(digest), 2);
  EXPECT_EQ(factory.count(another_digest), 2);
  EXPECT_EQ(factory.size(), 2);

  // Counts are not increased by moves.
  auto identifier_5 = std::move(identifier_4);
  EXPECT_EQ(factory.count(digest), 2);
  EXPECT_EQ(factory.count(another_digest), 2);
  EXPECT_EQ(factory.size(), 2);

  identifier_1 = ObjectIdentifier();
  EXPECT_EQ(factory.count(digest), 1);
  EXPECT_EQ(factory.count(another_digest), 2);
  EXPECT_EQ(factory.size(), 2);

  identifier_2 = ObjectIdentifier();
  EXPECT_EQ(factory.count(digest), 0);
  EXPECT_EQ(factory.count(another_digest), 2);
  EXPECT_EQ(factory.size(), 1);

  identifier_3 = ObjectIdentifier();
  EXPECT_EQ(factory.count(digest), 0);
  EXPECT_EQ(factory.count(another_digest), 1);
  EXPECT_EQ(factory.size(), 1);

  identifier_5 = ObjectIdentifier();
  EXPECT_EQ(factory.count(digest), 0);
  EXPECT_EQ(factory.count(another_digest), 0);
  EXPECT_EQ(factory.size(), 0);
}

TEST_F(ObjectIdentifierFactoryImplTest, ObjectOutlivingFactory) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifier identifier;

  {
    ObjectIdentifierFactoryImpl factory;
    EXPECT_EQ(factory.count(digest), 0);
    EXPECT_EQ(factory.size(), 0);

    identifier = factory.MakeObjectIdentifier(0u, 0u, digest);
    EXPECT_EQ(factory.count(digest), 1);
    EXPECT_EQ(factory.size(), 1);
    EXPECT_EQ(identifier.factory(), &factory);
  }

  // When the factory is destroyed, the identifier stops being tracked.
  EXPECT_EQ(identifier.factory(), nullptr);
}

}  // namespace
}  // namespace storage
