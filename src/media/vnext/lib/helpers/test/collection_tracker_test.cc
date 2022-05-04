// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/collection_tracker.h"

#include <gtest/gtest.h>

namespace fmlib {
namespace {

// Tests behavior in the initial state.
TEST(CollectionTracker, InitialState) {
  CollectionTracker<uint32_t> under_test;
  EXPECT_FALSE(under_test.is_dirty());
  auto actions = under_test.Clean();
  EXPECT_TRUE(actions.empty());
  EXPECT_FALSE(under_test.is_dirty());
}

// Tests effect of |OnAdded| on a clean tracker.
TEST(CollectionTracker, ItemAdded) {
  CollectionTracker<uint32_t> under_test;

  EXPECT_FALSE(under_test.is_dirty());
  under_test.OnAdded(0);
  EXPECT_TRUE(under_test.is_dirty());
  auto actions = under_test.Clean();
  EXPECT_FALSE(under_test.is_dirty());

  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kAdd, iter->second);
}

// Tests effect of |OnUpdated| on a clean tracker.
TEST(CollectionTracker, ItemUpdated) {
  CollectionTracker<uint32_t> under_test;

  // Need an item to update.
  under_test.OnAdded(0);
  under_test.Clean();

  EXPECT_FALSE(under_test.is_dirty());
  under_test.OnUpdated(0);
  EXPECT_TRUE(under_test.is_dirty());
  auto actions = under_test.Clean();
  EXPECT_FALSE(under_test.is_dirty());

  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kUpdate, iter->second);
}

// Tests effect of |OnRemoved| on a clean tracker.
TEST(CollectionTracker, ItemRemoved) {
  CollectionTracker<uint32_t> under_test;

  // Need an item to remove.
  under_test.OnAdded(0);
  under_test.Clean();

  EXPECT_FALSE(under_test.is_dirty());
  under_test.OnRemoved(0);
  EXPECT_TRUE(under_test.is_dirty());
  auto actions = under_test.Clean();
  EXPECT_FALSE(under_test.is_dirty());

  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kRemove, iter->second);
}

// Tests effect of |OnAdded| followed by |OnUpdated|.
TEST(CollectionTracker, ItemAddedAndUpdated) {
  CollectionTracker<uint32_t> under_test;

  under_test.OnAdded(0);
  // OnUpdated should have no effect.
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);

  auto actions = under_test.Clean();
  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kAdd, iter->second);
}

// Tests effect of |OnAdded| followed by |OnRemoved|.
TEST(CollectionTracker, ItemAddedAndRemoved) {
  CollectionTracker<uint32_t> under_test;

  under_test.OnAdded(0);
  // OnRemoved should undo OnAdded.
  under_test.OnRemoved(0);

  auto actions = under_test.Clean();
  EXPECT_TRUE(actions.empty());
}

// Tests effect of |OnRemoved| followed by |OnAdded|.
TEST(CollectionTracker, ItemRemovedAndAdded) {
  CollectionTracker<uint32_t> under_test;

  // Need an item to remove.
  under_test.OnAdded(0);
  under_test.Clean();

  under_test.OnRemoved(0);
  // OnAdded should turn the remove into an update.
  under_test.OnAdded(0);

  auto actions = under_test.Clean();
  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kUpdate, iter->second);
}

// Tests effect of |OnUpdated| followed by |OnRemoved|.
TEST(CollectionTracker, ItemUpdatedAndRemoved) {
  CollectionTracker<uint32_t> under_test;

  // Need an item to update.
  under_test.OnAdded(0);
  under_test.Clean();

  under_test.OnUpdated(0);
  // OnRemoved should turn the update into an remove.
  under_test.OnRemoved(0);

  auto actions = under_test.Clean();
  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kRemove, iter->second);
}

// Tests effect of repeated |OnUpdated| calls.
TEST(CollectionTracker, ItemUpdatedMuch) {
  CollectionTracker<uint32_t> under_test;

  // Need an item to update.
  under_test.OnAdded(0);
  under_test.Clean();

  under_test.OnUpdated(0);
  // Subsequent OnUpdateds should have no effect.
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);
  under_test.OnUpdated(0);

  auto actions = under_test.Clean();
  EXPECT_EQ(1u, actions.size());
  auto iter = actions.find(0);
  EXPECT_NE(actions.end(), iter);
  EXPECT_EQ(CleanAction::kUpdate, iter->second);
}

// Tests effect of adding, updating and removing many items.
TEST(CollectionTracker, ManyAddedUpdatedAndRemoved) {
  constexpr uint32_t kCount = 100;
  CollectionTracker<uint32_t> under_test;

  for (uint32_t id = 0; id < kCount; ++id) {
    under_test.OnAdded(id);
  }

  auto actions = under_test.Clean();
  EXPECT_EQ(kCount, actions.size());
  for (uint32_t id = 0; id < kCount; ++id) {
    auto iter = actions.find(id);
    EXPECT_NE(actions.end(), iter);
    EXPECT_EQ(CleanAction::kAdd, iter->second);
  }

  for (uint32_t id = 0; id < kCount; ++id) {
    under_test.OnUpdated(id);
  }

  actions = under_test.Clean();
  EXPECT_EQ(kCount, actions.size());
  for (uint32_t id = 0; id < kCount; ++id) {
    auto iter = actions.find(id);
    EXPECT_NE(actions.end(), iter);
    EXPECT_EQ(CleanAction::kUpdate, iter->second);
  }

  for (uint32_t id = 0; id < kCount; ++id) {
    under_test.OnRemoved(id);
  }

  actions = under_test.Clean();
  EXPECT_EQ(kCount, actions.size());
  for (uint32_t id = 0; id < kCount; ++id) {
    auto iter = actions.find(id);
    EXPECT_NE(actions.end(), iter);
    EXPECT_EQ(CleanAction::kRemove, iter->second);
  }
}

}  // namespace
}  // namespace fmlib
