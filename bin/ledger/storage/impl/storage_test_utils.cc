// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"

#include <inttypes.h>
#include <numeric>

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/impl/btree/builder.h"
#include "peridot/bin/ledger/storage/impl/btree/entry_change_iterator.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
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

ObjectIdentifier GetObjectIdentifier(std::string value) {
  std::string result;
  auto data_source = DataSource::Create(std::move(value));
  SplitDataSource(data_source.get(),
                  [&result](IterationStatus status, ObjectDigest object_digest,
                            std::unique_ptr<DataSource::DataChunk> chunk) {
                    if (status == IterationStatus::DONE) {
                      result = object_digest;
                    }
                    return encryption::MakeDefaultObjectIdentifier(
                        std::move(object_digest));
                  });
  return encryption::MakeDefaultObjectIdentifier(std::move(result));
}

// Pre-determined node level function.
uint8_t GetTestNodeLevel(convert::ExtendedStringView key) {
  if (key == "key03" || key == "key07" || key == "key30" || key == "key60" ||
      key == "key89") {
    return 1;
  }

  if (key == "key50" || key == "key75") {
    return 2;
  }

  return 0;
}

constexpr btree::NodeLevelCalculator kTestNodeLevelCalculator = {
    &GetTestNodeLevel};

}  // namespace

ObjectData::ObjectData(std::string value, InlineBehavior inline_behavior)
    : value(ResizeForBehavior(std::move(value), inline_behavior)),
      size(this->value.size()),
      object_identifier(GetObjectIdentifier(this->value)) {}

std::unique_ptr<DataSource> ObjectData::ToDataSource() {
  return DataSource::Create(value);
}

std::unique_ptr<DataSource::DataChunk> ObjectData::ToChunk() {
  return DataSource::DataChunk::Create(value);
}

ObjectDigest MakeObjectDigest(std::string content,
                              InlineBehavior inline_behavior) {
  return MakeObjectIdentifier(std::move(content), inline_behavior)
      .object_digest;
}

ObjectIdentifier MakeObjectIdentifier(std::string content,
                                      InlineBehavior inline_behavior) {
  ObjectData data(std::move(content), inline_behavior);
  return data.object_identifier;
}

std::string RandomString(size_t size) {
  std::string value;
  value.resize(size);
  zx_cprng_draw(&value[0], value.size());
  return value;
}

CommitId RandomCommitId() { return RandomString(kCommitIdSize); }

ObjectDigest RandomObjectDigest() {
  ObjectData data(RandomString(16), InlineBehavior::PREVENT);
  return data.object_identifier.object_digest;
}

ObjectIdentifier RandomObjectIdentifier() {
  return encryption::MakeDefaultObjectIdentifier(RandomObjectDigest());
}

EntryChange NewEntryChange(std::string key, std::string object_digest,
                           KeyPriority priority) {
  return EntryChange{
      Entry{std::move(key), MakeObjectIdentifier(std::move(object_digest)),
            priority},
      false};
}

EntryChange NewRemoveEntryChange(std::string key) {
  return EntryChange{
      Entry{std::move(key), MakeObjectIdentifier(""), KeyPriority::EAGER},
      true};
}

StorageTest::StorageTest() {}

StorageTest::~StorageTest() {}

::testing::AssertionResult StorageTest::AddObject(
    std::string value, std::unique_ptr<const Object>* object) {
  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  GetStorage()->AddObjectFromLocal(
      DataSource::Create(value),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &object_identifier));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "AddObjectFromLocal failed with status " << status
           << ". value: " << value;
  }

  std::unique_ptr<const Object> result;
  GetStorage()->GetObject(
      object_identifier, PageStorage::Location::LOCAL,
      callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "GetObject failed with status " << status << ". value: " << value
           << ", object_identifier: " << object_identifier;
  }
  object->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntries(
    size_t size, std::vector<Entry>* entries) {
  return CreateEntries(GetEnumeration(size), entries);
}

::testing::AssertionResult StorageTest::CreateEntries(
    std::vector<size_t> values, std::vector<Entry>* entries) {
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
                           object->GetIdentifier(), KeyPriority::EAGER});
  }
  entries->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntryChanges(
    size_t size, std::vector<EntryChange>* changes) {
  return CreateEntryChanges(GetEnumeration(size), changes, false);
}

::testing::AssertionResult StorageTest::CreateEntryChanges(
    std::vector<size_t> values, std::vector<EntryChange>* changes,
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

::testing::AssertionResult StorageTest::GetEmptyNodeIdentifier(
    ObjectIdentifier* empty_node_identifier) {
  bool called;
  Status status;
  btree::TreeNode::Empty(
      GetStorage(), callback::Capture(callback::SetWhenCalled(&called), &status,
                                      empty_node_identifier));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::Empty failed with status " << status;
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromIdentifier(
    ObjectIdentifier identifier, std::unique_ptr<const btree::TreeNode>* node) {
  bool called;
  Status status;
  std::unique_ptr<const btree::TreeNode> result;
  btree::TreeNode::FromIdentifier(
      GetStorage(), identifier,
      callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromDigest failed with status " << status;
  }
  node->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromEntries(
    const std::vector<Entry>& entries,
    const std::map<size_t, ObjectIdentifier>& children,
    std::unique_ptr<const btree::TreeNode>* node) {
  bool called;
  Status status;
  ObjectIdentifier identifier;
  btree::TreeNode::FromEntries(
      GetStorage(), 0u, entries, children,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &identifier));

  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromEntries failed with status " << status;
  }
  return CreateNodeFromIdentifier(std::move(identifier), node);
}

::testing::AssertionResult StorageTest::CreateTreeFromChanges(
    const ObjectIdentifier& base_node_identifier,
    const std::vector<EntryChange>& entries,
    ObjectIdentifier* new_root_identifier) {
  bool called;
  Status status;
  std::set<ObjectIdentifier> new_nodes;
  btree::ApplyChanges(
      environment_.coroutine_service(), GetStorage(), base_node_identifier,
      std::make_unique<btree::EntryChangeIterator>(entries.begin(),
                                                   entries.end()),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        new_root_identifier, &new_nodes),
      &kTestNodeLevelCalculator);
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "btree::ApplyChanges failed with status " << status;
  }
  return ::testing::AssertionSuccess();
}

}  // namespace storage
