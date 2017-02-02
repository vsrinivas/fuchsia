// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_

#include <string>

#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/socket/strings.h"

namespace storage {

// Creates a random id given its size.
std::string RandomId(size_t size);

// Creates the object id for testing from the given str, by resizing it as
// necessary.
ObjectId MakeObjectId(std::string str);

// Creates and returns a new EntryChange adding or updating the entry with the
// given information.
EntryChange NewEntryChange(std::string key,
                           std::string object_id,
                           KeyPriority priority);

// Creates and returns a new EntryChange removing the entry with the given key.
EntryChange NewRemoveEntryChange(std::string key);

// A TestWithMessageLoop providing some additional utility functions on
// PageStorage.
class StorageTest : public ::test::TestWithMessageLoop {
 protected:
  StorageTest();

  ~StorageTest();

  virtual PageStorage* GetStorage() = 0;

  // Adds a new object with the given value in the page storage and returns the
  // object.
  std::unique_ptr<const Object> AddObject(const std::string& value);

  // Returns a vector of entries, each of which has a key from "key00" to
  // "key01". A new value is created for each entry and the corresponding
  // object_id is set on the entry.
  std::vector<Entry> CreateEntries(int size);

  // Returns a vector of entry changes adding or updating the given number of
  // entries. See |CreateEntries| for information on the created entries.
  std::vector<EntryChange> CreateEntryChanges(int size);

  // Creates an empty tree node and returns its id.
  ObjectId GetEmptyNodeId();

  // Returns the tree node corresponding to the given id.
  std::unique_ptr<const TreeNode> CreateNodeFromId(ObjectIdView id);

  // Returns a new tree node containing the given entries and children.
  std::unique_ptr<const TreeNode> CreateNodeFromEntries(
      const std::vector<Entry>& entries,
      const std::vector<ObjectId>& children);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(StorageTest);
};

}  // storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_
