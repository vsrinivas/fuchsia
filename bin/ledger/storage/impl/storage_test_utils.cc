// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"

#include <inttypes.h>

#include <numeric>

#include "lib/fxl/strings/string_printf.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/object_id.h"
#include "peridot/bin/ledger/storage/impl/split.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {

namespace {
std::vector<size_t> GetEnumeration(size_t size) {
  FXL_CHECK(size <= 100);

  std::vector<size_t> values(size);
  std::iota(values.begin(), values.end(), 0u);

  return values;
}

std::string ResizeForBehavior(std::string value,
                              InlineBehavior inline_behavior) {
  if (inline_behavior == InlineBehavior::PREVENT &&
      value.size() <= kStorageHashSize) {
    value.resize(kStorageHashSize + 1);
  }
  return value;
}

std::string GetObjectId(std::string value) {
  std::string result;
  auto data_source = DataSource::Create(std::move(value));
  SplitDataSource(data_source.get(),
                  [&result](IterationStatus status, ObjectId object_id,
                            std::unique_ptr<DataSource::DataChunk> chunk) {
                    if (status == IterationStatus::DONE) {
                      result = object_id;
                    }
                  });
  return result;
}

}  // namespace

ObjectData::ObjectData(std::string value, InlineBehavior inline_behavior)
    : value(ResizeForBehavior(std::move(value), inline_behavior)),
      size(this->value.size()),
      object_id(GetObjectId(this->value)) {}

std::unique_ptr<DataSource> ObjectData::ToDataSource() {
  return DataSource::Create(value);
}

std::unique_ptr<DataSource::DataChunk> ObjectData::ToChunk() {
  return DataSource::DataChunk::Create(value);
}

ObjectId MakeObjectId(std::string content, InlineBehavior inline_behavior) {
  ObjectData data(std::move(content), inline_behavior);
  return data.object_id;
}

std::string RandomString(size_t size) {
  std::string value;
  value.resize(size);
  glue::RandBytes(&value[0], value.size());
  return value;
}

CommitId RandomCommitId() {
  return RandomString(kCommitIdSize);
}

ObjectId RandomObjectId() {
  ObjectData data(RandomString(16), InlineBehavior::PREVENT);
  return data.object_id;
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
    std::string value,
    std::unique_ptr<const Object>* object) {
  Status status;
  ObjectId object_id;
  GetStorage()->AddObjectFromLocal(
      DataSource::Create(value),
      callback::Capture(MakeQuitTask(), &status, &object_id));
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
  GetStorage()->GetObject(object_id, PageStorage::Location::LOCAL,
                          callback::Capture(MakeQuitTask(), &status, &result));
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
    size_t size,
    std::vector<Entry>* entries) {
  return CreateEntries(GetEnumeration(size), entries);
}

::testing::AssertionResult StorageTest::CreateEntries(
    std::vector<size_t> values,
    std::vector<Entry>* entries) {
  std::vector<Entry> result;
  for (auto i : values) {
    FXL_DCHECK(i < 100);
    std::unique_ptr<const Object> object;
    ::testing::AssertionResult assertion_result =
        AddObject(fxl::StringPrintf("object%02" PRIuMAX, i), &object);
    if (!assertion_result) {
      return assertion_result;
    }
    result.push_back(Entry{fxl::StringPrintf("key%02" PRIuMAX, i),
                           object->GetId(), KeyPriority::EAGER});
  }
  entries->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntryChanges(
    size_t size,
    std::vector<EntryChange>* changes) {
  return CreateEntryChanges(GetEnumeration(size), changes, false);
}

::testing::AssertionResult StorageTest::CreateEntryChanges(
    std::vector<size_t> values,
    std::vector<EntryChange>* changes,
    bool deletion) {
  std::vector<Entry> entries;
  ::testing::AssertionResult assertion_result =
      CreateEntries(std::move(values), &entries);
  if (!assertion_result) {
    return assertion_result;
  }
  std::vector<EntryChange> result;

  result.reserve(entries.size());
  for (auto& entry : entries) {
    result.push_back(EntryChange{std::move(entry), deletion});
  }
  changes->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::GetEmptyNodeId(
    ObjectId* empty_node_id) {
  Status status;
  ObjectId id;
  btree::TreeNode::Empty(GetStorage(),
                         callback::Capture(MakeQuitTask(), &status, &id));
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
    std::unique_ptr<const btree::TreeNode>* node) {
  Status status;
  std::unique_ptr<const btree::TreeNode> result;
  btree::TreeNode::FromId(GetStorage(), id,
                          callback::Capture(MakeQuitTask(), &status, &result));
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
    std::unique_ptr<const btree::TreeNode>* node) {
  Status status;
  ObjectId id;
  btree::TreeNode::FromEntries(GetStorage(), 0u, entries, children,
                               callback::Capture(MakeQuitTask(), &status, &id));

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

}  // namespace storage
