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

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

using ObjectIdentifierFactoryImplTest = ledger::TestWithEnvironment;

TEST_F(ObjectIdentifierFactoryImplTest, CountsAndCleansUp) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  const ObjectDigest another_digest = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory;
  EXPECT_EQ(factory.count(digest), 0);
  EXPECT_EQ(factory.size(), 0);

  auto identifier_1 = factory.MakeObjectIdentifier(0u, digest);
  EXPECT_EQ(factory.count(digest), 1);
  EXPECT_EQ(factory.size(), 1);

  // Tracking is per-digest, not per-identifier.
  auto identifier_2 = factory.MakeObjectIdentifier(1u, digest);
  EXPECT_EQ(factory.count(digest), 2);
  EXPECT_EQ(factory.size(), 1);

  // Distinct digests are tracked separately.
  auto identifier_3 = factory.MakeObjectIdentifier(0u, another_digest);
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

    identifier = factory.MakeObjectIdentifier(0u, digest);
    EXPECT_EQ(factory.count(digest), 1);
    EXPECT_EQ(factory.size(), 1);
    EXPECT_EQ(identifier.factory(), &factory);
  }

  // When the factory is destroyed, the identifier stops being tracked.
  EXPECT_EQ(identifier.factory(), nullptr);
}

TEST_F(ObjectIdentifierFactoryImplTest, DecodingInvalidObjectDigest) {
  const ObjectDigest digest("INVALID");
  ObjectIdentifier identifier(0, digest, nullptr);
  ObjectIdentifierFactoryImpl factory;
  std::string encoded = factory.ObjectIdentifierToStorageBytes(identifier);
  ASSERT_FALSE(factory.MakeObjectIdentifierFromStorageBytes(encoded, &identifier));
}

TEST_F(ObjectIdentifierFactoryImplTest, TrackDeletionSuccess) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifierFactoryImpl factory;
  EXPECT_TRUE(factory.TrackDeletion(digest));
}

TEST_F(ObjectIdentifierFactoryImplTest, TrackDeletionAlreadyPending) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifierFactoryImpl factory;
  EXPECT_TRUE(factory.TrackDeletion(digest));
  EXPECT_FALSE(factory.TrackDeletion(digest));
}

TEST_F(ObjectIdentifierFactoryImplTest, TrackDeletionCurrentlyTracked) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifierFactoryImpl factory;
  {
    auto identifier = factory.MakeObjectIdentifier(0u, digest);
    EXPECT_FALSE(factory.TrackDeletion(digest));
  }
  EXPECT_TRUE(factory.TrackDeletion(digest));
}

TEST_F(ObjectIdentifierFactoryImplTest, UntrackDeletion) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifierFactoryImpl factory;
  EXPECT_TRUE(factory.TrackDeletion(digest));
  EXPECT_TRUE(factory.UntrackDeletion(digest));
}

TEST_F(ObjectIdentifierFactoryImplTest, AllocatingIdentifierImplicitlyAborts) {
  const ObjectDigest digest = RandomObjectDigest(environment_.random());
  ObjectIdentifierFactoryImpl factory;
  EXPECT_TRUE(factory.TrackDeletion(digest));
  {
    // Allocate and immediately throw away an identifier for |digest|.
    auto identifier = factory.MakeObjectIdentifier(0u, digest);
  }
  // Allocating an identifier aborts the pending transaction, even if the identifier is not live
  // anymore when completing.
  EXPECT_FALSE(factory.UntrackDeletion(digest));

  // Perform another aborted deletion cycle to catch a bug where aborted deletions are not cleaned
  // up.
  EXPECT_TRUE(factory.TrackDeletion(digest));
  factory.MakeObjectIdentifier(0u, digest);
  EXPECT_FALSE(factory.UntrackDeletion(digest));

  // Perform a full deletion cycle after an aborted one.
  EXPECT_TRUE(factory.TrackDeletion(digest));
  EXPECT_TRUE(factory.UntrackDeletion(digest));
}

TEST_F(ObjectIdentifierFactoryImplTest, NeverPolicyUntrackedCallback) {
  // With NotificationPolicy::NEVER, setting the untracked callback or calling |NotifyOnUntracked|
  // should have no effect.

  const ObjectDigest digest1 = RandomObjectDigest(environment_.random());
  const ObjectDigest digest2 = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory(ObjectIdentifierFactoryImpl::NotificationPolicy::NEVER);

  bool called = false;
  factory.SetUntrackedCallback([&](const ObjectDigest& digest) { called = true; });

  {
    auto identifier1 = factory.MakeObjectIdentifier(0u, digest1);
    auto identifier2 = factory.MakeObjectIdentifier(0u, digest2);

    // Calling |NotifyOnUntracked| on digest2 should still have no effect.
    factory.NotifyOnUntracked(digest2);
    EXPECT_FALSE(called);
  }
  // None of the two identifiers should receive a notification.
  EXPECT_FALSE(called);
}

TEST_F(ObjectIdentifierFactoryImplTest, OnMarkedObjectsOnlyPolicyUntrackedCallback) {
  // With NotificationPolicy::, the untracked callback should only be called for those objects that
  // |NotifyOnUntracked| has been called.

  const ObjectDigest digest_to_notify = RandomObjectDigest(environment_.random());
  const ObjectDigest other_digest = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory(
      ObjectIdentifierFactoryImpl::NotificationPolicy::ON_MARKED_OBJECTS_ONLY);
  bool called = false;
  factory.SetUntrackedCallback([&](const ObjectDigest& digest) {
    // This callback should only be called on |digest_to_notify| since |NotifyOnUntracked| is only
    // called on that one.
    EXPECT_EQ(digest, digest_to_notify);
    called = true;
  });

  {
    auto identifier_to_notify = factory.MakeObjectIdentifier(0u, digest_to_notify);
    auto other_identifier = factory.MakeObjectIdentifier(0u, other_digest);

    factory.NotifyOnUntracked(digest_to_notify);
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);
}

TEST_F(ObjectIdentifierFactoryImplTest, OnMarkedObjectsOnlyPolicyNotifyImmediatelyWhenNoLiveRefs) {
  // With NotificationPolicy::ON_MARKED_OBJECTS_ONLY, if an object already has 0 live references,
  // the untracked callback should be called immediately when |NotifyOnUtracked| is called.

  const ObjectDigest digest_to_notify = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory(
      ObjectIdentifierFactoryImpl::NotificationPolicy::ON_MARKED_OBJECTS_ONLY);
  bool called = false;
  factory.SetUntrackedCallback([&](const ObjectDigest& digest) {
    EXPECT_EQ(digest, digest_to_notify);
    called = true;
  });

  // There are no live references: make sure the untracked callback is called immediately.
  factory.NotifyOnUntracked(digest_to_notify);
  EXPECT_TRUE(called);
}

TEST_F(ObjectIdentifierFactoryImplTest, OnMarkedObjectsOnlyPolicyDoNotNotifyTwice) {
  // With NotificationPolicy::ON_MARKED_OBJECTS_ONLY, the untracked callback should only be called
  // once for each call to |NotifyOnUtracked|.

  const ObjectDigest digest_to_notify = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory(
      ObjectIdentifierFactoryImpl::NotificationPolicy::ON_MARKED_OBJECTS_ONLY);
  bool called = false;
  factory.SetUntrackedCallback([&](const ObjectDigest& digest) {
    EXPECT_EQ(digest, digest_to_notify);
    called = true;
  });

  // Make sure the notification is sent once.
  {
    auto identifier = factory.MakeObjectIdentifier(0u, digest_to_notify);
    factory.NotifyOnUntracked(digest_to_notify);
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);

  called = false;
  // Now, the object should no longer be marked to be notified: if it has live references again we
  // shouldn't call the callback.
  {
    auto identifier = factory.MakeObjectIdentifier(0u, digest_to_notify);
    EXPECT_FALSE(called);
  }
  EXPECT_FALSE(called);

  // Make sure it is possible to send notifications for the same object if |NotifyOnUntracked| is
  // called.
  factory.NotifyOnUntracked(digest_to_notify);
  EXPECT_TRUE(called);
}

TEST_F(ObjectIdentifierFactoryImplTest, AlwaysPolicyUntrackedCallback) {
  // With NotificationPolicy::ALWAYS, the untracked callback should be called on all objects.
  // Calling |NotifyOnUntracked| should have no effect.

  const ObjectDigest digest1 = RandomObjectDigest(environment_.random());
  const ObjectDigest digest2 = RandomObjectDigest(environment_.random());

  ObjectIdentifierFactoryImpl factory(ObjectIdentifierFactoryImpl::NotificationPolicy::ALWAYS);

  std::vector<ObjectDigest> digests_called;
  factory.SetUntrackedCallback(
      [&](const ObjectDigest& digest) { digests_called.push_back(digest); });

  {
    auto identifier1 = factory.MakeObjectIdentifier(0u, digest1);
    auto identifier2 = factory.MakeObjectIdentifier(0u, digest2);

    // Calling |NotifyOnUntracked| on digest2 should have no effect: both should receive the
    // notification.
    factory.NotifyOnUntracked(digest2);
    EXPECT_THAT(digests_called, IsEmpty());
  }
  // Both identifiers should receive a notification.
  EXPECT_THAT(digests_called, UnorderedElementsAre(digest1, digest2));
}

}  // namespace
}  // namespace storage
