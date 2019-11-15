// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/test_utils.h"

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {

TestWithPageStorage::TestWithPageStorage() : encryption_service_(dispatcher()) {}

TestWithPageStorage::~TestWithPageStorage() = default;

fit::function<void(storage::Journal*)> TestWithPageStorage::AddKeyValueToJournal(
    const std::string& key, std::string value) {
  return [this, key, value = std::move(value)](storage::Journal* journal) mutable {
    Status status;
    storage::ObjectIdentifier object_identifier;
    bool called;
    page_storage()->AddObjectFromLocal(
        storage::ObjectType::BLOB, storage::DataSource::Create(std::move(value)), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    journal->Put(key, object_identifier, storage::KeyPriority::EAGER);
  };
}

fit::function<void(storage::Journal*)> TestWithPageStorage::DeleteKeyFromJournal(
    const std::string& key) {
  return [key](storage::Journal* journal) { journal->Delete(key); };
}

::testing::AssertionResult TestWithPageStorage::GetValue(
    storage::ObjectIdentifier object_identifier, std::string* value) {
  Status status;
  std::unique_ptr<const storage::Object> object;
  bool called;
  page_storage()->GetObject(std::move(object_identifier), storage::PageStorage::Location::Local(),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &object));
  RunLoopUntilIdle();
  if (!called) {
    return ::testing::AssertionFailure() << "PageStorage::GetObject never called the callback.";
  }
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "PageStorage::GetObject returned status: " << status;
  }

  fxl::StringView data;
  status = object->GetData(&data);
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "Object::GetData returned status: " << status;
  }

  *value = data.ToString();
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult TestWithPageStorage::CreatePageStorage(
    std::unique_ptr<storage::PageStorage>* page_storage) {
  auto db =
      std::make_unique<storage::LevelDb>(environment_.dispatcher(), DetachedPath(tmpfs_.root_fd()));
  Status status = db->Init();
  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "LevelDb::Init failed with status " << status;
  }
  auto local_page_storage = std::make_unique<storage::PageStorageImpl>(
      &environment_, &encryption_service_, std::move(db), kRootPageId.ToString(),
      storage::CommitPruningPolicy::NEVER);

  bool called;
  clocks::DeviceIdManagerEmptyImpl device_id_manager;
  local_page_storage->Init(&device_id_manager,
                           callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  if (!called) {
    return ::testing::AssertionFailure() << "PageStorage::Init never called the callback.";
  }

  if (status != Status::OK) {
    return ::testing::AssertionFailure() << "PageStorageImpl::Init returned status: " << status;
  }
  *page_storage = std::move(local_page_storage);
  return ::testing::AssertionSuccess();
}

fit::closure TestWithPageStorage::MakeQuitTaskOnce() {
  return [this, called = false]() mutable {
    if (!called) {
      called = true;
      QuitLoop();
    }
  };
}

}  // namespace ledger
