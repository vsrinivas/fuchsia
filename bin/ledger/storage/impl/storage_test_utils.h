// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_STORAGE_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_STORAGE_TEST_UTILS_H_

#include <string>

#include <lib/fsl/socket/strings.h>
#include <lib/fxl/functional/closure.h>

#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/testing/test_with_coroutines.h"

namespace storage {

// A sufficiently large delay, such that if a storagemethod posts a delayed
// task, the task will be due after associated amount of time.
constexpr zx::duration kSufficientDelay = zx::hour(1);

// Enum describing the expected behavior for identifier, allowing or preventing
// to be inlined values.
enum class InlineBehavior {
  ALLOW,
  PREVENT,
};

class ObjectData {
 public:
  explicit ObjectData(std::string value,
                      InlineBehavior inline_behavior = InlineBehavior::ALLOW);
  std::unique_ptr<DataSource> ToDataSource();
  std::unique_ptr<DataSource::DataChunk> ToChunk();

  const std::string value;
  const size_t size;
  const ObjectIdentifier object_identifier;
};

// Builder the object digest for the given content. If |inline_behavior| is
// InlineBehavior::PREVENT, resize |content| so that it cannot be inlined.
ObjectDigest MakeObjectDigest(
    std::string content,
    InlineBehavior inline_behavior = InlineBehavior::ALLOW);

// Builder the object identifier for the given content. If |inline_behavior| is
// InlineBehavior::PREVENT, resize |content| so that it cannot be inlined.
ObjectIdentifier MakeObjectIdentifier(
    std::string content,
    InlineBehavior inline_behavior = InlineBehavior::ALLOW);

// Returns a random string of the given length.
std::string RandomString(size_t size);

// Create a new random commit id.
CommitId RandomCommitId();

// Create a new random object digest.
ObjectDigest RandomObjectDigest();

// Create a new random object identifier.
ObjectIdentifier RandomObjectIdentifier();

// Creates and returns a new EntryChange adding or updating the entry with the
// given information.
EntryChange NewEntryChange(std::string key, std::string object_digest,
                           KeyPriority priority);

// Creates and returns a new EntryChange removing the entry with the given key.
EntryChange NewRemoveEntryChange(std::string key);

// A TestLoopFixture providing some additional utility functions on PageStorage.
//
// All utility functions in this class return an |AssertionResult| meaning that
// they can be used in an EXPECT/ASSERT_TRUE: E.g.
//     ASSERT_TRUE(AddObject("value", &object));
// or an EXPECT/ASSERT_FALSE if the function is expected to fail.
//     ASSERT_FALSE(AddObject("value", &object));
class StorageTest : public ::test::TestWithCoroutines {
 protected:
  StorageTest();

  ~StorageTest() override;

  virtual PageStorage* GetStorage() = 0;

  // Adds a new object with the given value in the page storage and updates
  // |object| with the new value.
  ::testing::AssertionResult AddObject(std::string value,
                                       std::unique_ptr<const Object>* object);

  // Creates a vector of entries, each of which has a key from "key00" to
  // "keyXX" where XX is |size-1|. A new value is created for each entry and the
  // corresponding object_digest is set on the entry. |entries| vector will be
  // swapped with the result.
  ::testing::AssertionResult CreateEntries(size_t size,
                                           std::vector<Entry>* entries);

  // Creates a vector of entries, each of which has a key "keyXX", were "XX" is
  // taken from the |values| vector. A new value is created for each entry and
  // the corresponding object_digest is set on the entry. |entries| vector will
  // be swapped with the result.
  ::testing::AssertionResult CreateEntries(std::vector<size_t> values,
                                           std::vector<Entry>* entries);

  // Creates a vector of entry changes adding or updating the given number of
  // entries. See |CreateEntries| for information on the created entries.
  // |changes| vector will be swapped with the result.
  ::testing::AssertionResult CreateEntryChanges(
      size_t size, std::vector<EntryChange>* changes);

  // Creates a vector of entry changes adding or updating the given number of
  // entries. See |CreateEntries| for information on the created entries.
  // |changes| vector will be swapped with the result. If |deletion| is true,
  // the changes will be deletions, otherwise the changes will be updates.
  ::testing::AssertionResult CreateEntryChanges(
      std::vector<size_t> values, std::vector<EntryChange>* changes,
      bool deletion = false);

  // Creates an empty tree node and updates |empty_node_identifier| with the
  // result.
  ::testing::AssertionResult GetEmptyNodeIdentifier(
      ObjectIdentifier* empty_node_identifier);

  // Returns the tree node corresponding to the given id.
  ::testing::AssertionResult CreateNodeFromIdentifier(
      ObjectIdentifier identifier,
      std::unique_ptr<const btree::TreeNode>* node);

  // Creates a new tree node from the given entries and children and updates
  // |node| with the result.
  ::testing::AssertionResult CreateNodeFromEntries(
      const std::vector<Entry>& entries,
      const std::map<size_t, ObjectIdentifier>& children,
      std::unique_ptr<const btree::TreeNode>* node);

  // Creates a BTree applying changes from the base node and gives back the
  // digest of its new root node.
  ::testing::AssertionResult CreateTreeFromChanges(
      const ObjectIdentifier& base_node_identifier,
      const std::vector<EntryChange>& entries,
      ObjectIdentifier* new_root_identifier);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(StorageTest);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_STORAGE_TEST_UTILS_H_
