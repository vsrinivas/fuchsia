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

::testing::AssertionResult StorageTest::AddObject(
    const std::string& value,
    std::unique_ptr<const Object>* object) {
  Status status;
  ObjectId object_id;
  GetStorage()->AddObjectFromLocal(
      mtl::WriteStringToSocket(value), value.size(),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &object_id));
  if (RunLoopWithTimeout()) {
    return ::testing::AssertionFailure()
           << "AddObjectFromLocal callback was not executed. value: " << value;
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "AddObjectFromLocal failed with status " << status
           << ". value: " << value;
  }

  std::unique_ptr<const Object> result;
  GetStorage()->GetObject(
      object_id,
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &result));
  if (RunLoopWithTimeout()) {
    return ::testing::AssertionFailure()
           << "GetObject callback was not executed. value: " << value
           << ", object_id: " << object_id;
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "GetObject failed with status " << status << ". value: " << value
           << ", object_id: " << object_id;
  }
  object->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntries(
    int size,
    std::vector<Entry>* entries) {
  FTL_CHECK(size >= 0 && size <= 100);
  std::vector<Entry> result;
  for (int i = 0; i < size; ++i) {
    std::unique_ptr<const Object> object;
    ::testing::AssertionResult assertion_result =
        AddObject(ftl::StringPrintf("object%02d", i), &object);
    if (!assertion_result) {
      return assertion_result;
    }
    result.push_back(Entry{ftl::StringPrintf("key%02d", i), object->GetId(),
                           KeyPriority::EAGER});
  }
  entries->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntryChanges(
    int size,
    std::vector<EntryChange>* changes) {
  std::vector<Entry> entries;
  ::testing::AssertionResult assertion_result = CreateEntries(size, &entries);
  if (!assertion_result) {
    return assertion_result;
  }
  std::vector<EntryChange> result;

  for (int i = 0; i < size; ++i) {
    result.push_back(EntryChange{std::move(entries[i]), false});
  }
  changes->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::GetEmptyNodeId(
    ObjectId* empty_node_id) {
  Status status;
  ObjectId id;
  TreeNode::Empty(
      GetStorage(),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status, &id));
  if (RunLoopWithTimeout()) {
    return ::testing::AssertionFailure()
           << "TreeNode::Empty callback was not executed.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::Empty failed with status " << status;
  }
  empty_node_id->swap(id);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromId(
    ObjectIdView id,
    std::unique_ptr<const TreeNode>* node) {
  Status status;
  std::unique_ptr<const TreeNode> result;
  TreeNode::FromId(GetStorage(), id,
                   ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                   &status, &result));
  if (RunLoopWithTimeout()) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromId callback was not executed.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromId failed with status " << status;
  }
  node->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromEntries(
    const std::vector<Entry>& entries,
    const std::vector<ObjectId>& children,
    std::unique_ptr<const TreeNode>* node) {
  Status status;
  ObjectId id;
  TreeNode::FromEntries(
      GetStorage(), entries, children,
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status, &id));

  if (RunLoopWithTimeout()) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromEntries callback was not executed.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromEntries failed with status " << status;
  }
  return CreateNodeFromId(id, node);
}

}  // storage
