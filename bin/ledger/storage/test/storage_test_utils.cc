// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/test/storage_test_utils.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/test/capture.h"
#include "lib/ftl/strings/string_printf.h"

namespace storage {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

ObjectId MakeObjectId(std::string str) {
  // Resize id to the required size, adding trailing underscores if needed.
  str.resize(kObjectIdSize, '_');
  return str;
}

EntryChange NewEntryChange(std::string key,
                           std::string object_id,
                           KeyPriority priority) {
  return EntryChange{Entry{std::move(key), std::move(object_id), priority},
                     false};
}

EntryChange NewRemoveEntryChange(std::string key) {
  return EntryChange{Entry{std::move(key), "", KeyPriority::EAGER}, true};
}

StorageTest::StorageTest(){};

StorageTest::~StorageTest(){};

std::unique_ptr<const Object> StorageTest::AddObject(const std::string& value) {
  Status status;
  ObjectId object_id;
  GetStorage()->AddObjectFromLocal(
      mtl::WriteStringToSocket(value), value.size(),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &object_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  std::unique_ptr<const Object> object;
  GetStorage()->GetObject(
      object_id,
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &object));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  return object;
}

std::vector<Entry> StorageTest::CreateEntries(int size) {
  FTL_CHECK(size >= 0 && size <= 100);
  std::vector<Entry> result;
  for (int i = 0; i < size; ++i) {
    std::unique_ptr<const Object> object =
        AddObject(ftl::StringPrintf("object%02d", i));
    result.push_back(Entry{ftl::StringPrintf("key%02d", i), object->GetId(),
                           KeyPriority::EAGER});
  }
  return result;
}

std::vector<EntryChange> StorageTest::CreateEntryChanges(int size) {
  std::vector<Entry> entries = CreateEntries(size);
  std::vector<EntryChange> result;

  for (int i = 0; i < size; ++i) {
    result.push_back(EntryChange{std::move(entries[i]), false});
  }
  return result;
}

ObjectId StorageTest::GetEmptyNodeId() {
  Status status;
  ObjectId id;
  TreeNode::Empty(
      GetStorage(),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status, &id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  return id;
}

std::unique_ptr<const TreeNode> StorageTest::CreateNodeFromId(ObjectIdView id) {
  Status status;
  std::unique_ptr<const TreeNode> node;
  TreeNode::FromId(GetStorage(), id,
                   ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                   &status, &node));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  return node;
}

std::unique_ptr<const TreeNode> StorageTest::CreateNodeFromEntries(
    const std::vector<Entry>& entries,
    const std::vector<ObjectId>& children) {
  Status status;
  ObjectId id;
  TreeNode::FromEntries(
      GetStorage(), entries, children,
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status, &id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  return CreateNodeFromId(id);
}

}  // storage
