// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/storage_test_utils.h"

#include <inttypes.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/impl/btree/builder.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/split.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

namespace storage {

namespace {

std::vector<size_t> GetEnumeration(size_t size) {
  std::vector<size_t> values(size);
  std::iota(values.begin(), values.end(), 0u);

  return values;
}

std::string ResizeForBehavior(std::string value, InlineBehavior inline_behavior) {
  if (inline_behavior == InlineBehavior::PREVENT && value.size() <= kStorageHashSize) {
    value.resize(kStorageHashSize + 1);
  }
  return value;
}

ObjectIdentifier GetObjectIdentifier(std::string value, ObjectType object_type,
                                     ObjectIdentifierFactory* factory) {
  return ForEachPiece(std::move(value), object_type, factory,
                      [](std::unique_ptr<const Piece> /*piece*/) {});
}

// Pre-determined node level function.
uint8_t GetTestNodeLevel(convert::ExtendedStringView key) {
  if (key == "key03" || key == "key07" || key == "key30" || key == "key60" || key == "key89") {
    return 1;
  }

  if (key == "key50" || key == "key75") {
    return 2;
  }

  return 0;
}

constexpr btree::NodeLevelCalculator kTestNodeLevelCalculator = {&GetTestNodeLevel};

}  // namespace

ObjectData::ObjectData(ObjectIdentifierFactory* factory, std::string value, ObjectType object_type,
                       InlineBehavior inline_behavior)
    : value(ResizeForBehavior(std::move(value), inline_behavior)),
      size(this->value.size()),
      object_identifier(GetObjectIdentifier(this->value, object_type, factory)) {}

std::unique_ptr<DataSource> ObjectData::ToDataSource() { return DataSource::Create(value); }

std::unique_ptr<DataSource::DataChunk> ObjectData::ToChunk() {
  return DataSource::DataChunk::Create(value);
}

std::unique_ptr<Piece> ObjectData::ToPiece() {
  return std::make_unique<DataChunkPiece>(object_identifier, ToChunk());
}

ObjectDigest MakeObjectDigest(std::string content, InlineBehavior inline_behavior) {
  return MakeObjectIdentifier(std::move(content), inline_behavior).object_digest();
}

ObjectIdentifier MakeObjectIdentifier(std::string content, InlineBehavior inline_behavior) {
  fake::FakeObjectIdentifierFactory factory;
  ObjectData data(&factory, std::move(content), inline_behavior);
  return data.object_identifier;
}

ObjectIdentifier ForEachPiece(std::string content, ObjectType type,
                              ObjectIdentifierFactory* factory,
                              fit::function<void(std::unique_ptr<const Piece>)> callback) {
  ObjectDigest result;
  auto data_source = DataSource::Create(std::move(content));
  SplitDataSource(
      data_source.get(), type,
      [factory](ObjectDigest object_digest) {
        return encryption::MakeDefaultObjectIdentifier(factory, std::move(object_digest));
      },
      [](uint64_t chunk_window_hash) { return encryption::DefaultPermutation(chunk_window_hash); },
      [&result, callback = std::move(callback)](IterationStatus status,
                                                std::unique_ptr<Piece> piece) {
        if (status == IterationStatus::DONE) {
          result = piece->GetIdentifier().object_digest();
        }
        callback(std::move(piece));
      });
  return encryption::MakeDefaultObjectIdentifier(factory, std::move(result));
}

std::string RandomString(rng::Random* random, size_t size) {
  std::string value;
  value.resize(size);
  random->Draw(&value);
  return value;
}

CommitId RandomCommitId(rng::Random* random) { return RandomString(random, kCommitIdSize); }

ObjectDigest RandomObjectDigest(rng::Random* random) {
  fake::FakeObjectIdentifierFactory factory;
  ObjectData data(&factory, RandomString(random, 16), InlineBehavior::PREVENT);
  return data.object_identifier.object_digest();
}

ObjectIdentifier RandomObjectIdentifier(rng::Random* random, ObjectIdentifierFactory* factory) {
  return encryption::MakeDefaultObjectIdentifier(factory, RandomObjectDigest(random));
}

EntryChange NewEntryChange(std::string key, std::string object_digest, KeyPriority priority) {
  EntryId id = "id" + key;
  return EntryChange{Entry{std::move(key), MakeObjectIdentifier(std::move(object_digest)), priority,
                           std::move(id)},
                     false};
}

EntryChange NewRemoveEntryChange(std::string key) {
  EntryId id = "id" + key;
  return EntryChange{
      Entry{std::move(key), MakeObjectIdentifier(""), KeyPriority::EAGER, std::move(id)}, true};
}

std::vector<Entry> WithoutEntryIds(std::vector<Entry> entries) {
  for (auto& entry : entries) {
    entry.entry_id = "";
  }
  return entries;
}

ThreeWayChange WithoutEntryIds(const ThreeWayChange& change) {
  ThreeWayChange result;
  if (change.base) {
    result.base = std::make_unique<Entry>(*change.base);
    result.base->entry_id = "";
  }
  if (change.left) {
    result.left = std::make_unique<Entry>(*change.left);
    result.left->entry_id = "";
  }
  if (change.right) {
    result.right = std::make_unique<Entry>(*change.right);
    result.right->entry_id = "";
  }
  return result;
}

Entry WithoutEntryId(Entry e) {
  e.entry_id = "";
  return e;
}

StorageTest::StorageTest() = default;

StorageTest::StorageTest(GarbageCollectionPolicy gc_policy)
    : ledger::TestWithEnvironment(
          [gc_policy](ledger::EnvironmentBuilder* builder) { builder->SetGcPolicy(gc_policy); }) {}

StorageTest::~StorageTest() = default;

::testing::AssertionResult StorageTest::AddObject(std::string value,
                                                  std::unique_ptr<const Object>* object) {
  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  GetStorage()->AddObjectFromLocal(
      ObjectType::BLOB, DataSource::Create(value), {},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "AddObjectFromLocal callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "AddObjectFromLocal failed with status " << status << ". value: " << value;
  }

  std::unique_ptr<const Object> result;
  GetStorage()->GetObject(object_identifier, PageStorage::Location::Local(),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "GetObject callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "GetObject failed with status " << status << ". value: " << value
           << ", object_identifier: " << object_identifier;
  }
  object->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntries(size_t size, std::vector<Entry>* entries) {
  return CreateEntries(GetEnumeration(size), entries);
}

::testing::AssertionResult StorageTest::CreateEntries(std::vector<size_t> values,
                                                      std::vector<Entry>* entries) {
  std::vector<Entry> result;
  size_t largest = 0;
  if (auto it = std::max_element(values.begin(), values.end()); it != values.end()) {
    largest = *it;
  }
  // For compatibility with existing uses, length is at least two.
  int length = 1 + std::floor(std::log10(std::max(largest, (size_t)99)));
  for (auto i : values) {
    std::unique_ptr<const Object> object;
    ::testing::AssertionResult assertion_result =
        AddObject(absl::StrFormat("object%0*" PRIuMAX, length, i), &object);
    if (!assertion_result) {
      return assertion_result;
    }
    result.push_back(Entry{absl::StrFormat("key%0*" PRIuMAX, length, i), object->GetIdentifier(),
                           KeyPriority::EAGER,
                           EntryId(absl::StrFormat("id_%0*" PRIuMAX, length, i))});
  }
  entries->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateEntryChanges(size_t size,
                                                           std::vector<EntryChange>* changes) {
  return CreateEntryChanges(GetEnumeration(size), changes, false);
}

::testing::AssertionResult StorageTest::CreateEntryChanges(std::vector<size_t> values,
                                                           std::vector<EntryChange>* changes,
                                                           bool deletion) {
  std::vector<Entry> entries;
  ::testing::AssertionResult assertion_result = CreateEntries(std::move(values), &entries);
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
  btree::TreeNode::Empty(GetStorage(), callback::Capture(callback::SetWhenCalled(&called), &status,
                                                         empty_node_identifier));
  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "TreeNode::Empty callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "TreeNode::Empty failed with status " << status;
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromIdentifier(
    ObjectIdentifier identifier, PageStorage::Location location,
    std::unique_ptr<const btree::TreeNode>* node) {
  bool called;
  Status status;
  std::unique_ptr<const btree::TreeNode> result;
  btree::TreeNode::FromIdentifier(
      GetStorage(), {std::move(identifier), std::move(location)},
      callback::Capture(callback::SetWhenCalled(&called), &status, &result));
  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "TreeNode::FromIdentifier callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure()
           << "TreeNode::FromIdentifier failed with status " << status;
  }
  node->swap(result);
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult StorageTest::CreateNodeFromEntries(
    const std::vector<Entry>& entries, const std::map<size_t, ObjectIdentifier>& children,
    std::unique_ptr<const btree::TreeNode>* node) {
  bool called;
  Status status;
  ObjectIdentifier identifier;
  btree::TreeNode::FromEntries(
      GetStorage(), 0u, entries, children,
      callback::Capture(callback::SetWhenCalled(&called), &status, &identifier));

  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "TreeNode::FromEntries callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "TreeNode::FromEntries failed with status " << status;
  }
  return CreateNodeFromIdentifier(std::move(identifier), PageStorage::Location::Local(), node);
}

::testing::AssertionResult StorageTest::CreateTreeFromChanges(
    const ObjectIdentifier& base_node_identifier, const std::vector<EntryChange>& entries,
    ObjectIdentifier* new_root_identifier) {
  bool called = false;
  Status status;
  std::set<ObjectIdentifier> new_nodes;
  coroutine::CoroutineManager coroutine_manager(environment_.coroutine_service());

  coroutine_manager.StartCoroutine([&](coroutine::CoroutineHandler* handler) {
    status = btree::ApplyChanges(handler, GetStorage(),
                                 {base_node_identifier, PageStorage::Location::Local()}, entries,
                                 new_root_identifier, &new_nodes, &kTestNodeLevelCalculator);
    called = true;
  });
  RunLoopFor(kSufficientDelay);
  if (!called) {
    return ::testing::AssertionFailure() << "btree::ApplyChanges callback wasn't called.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "btree::ApplyChanges failed with status " << status;
  }
  return ::testing::AssertionSuccess();
}

}  // namespace storage
