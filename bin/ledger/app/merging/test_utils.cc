// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/test_utils.h"

#include <lib/async/dispatcher.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace ledger {
TestBackoff::TestBackoff(int* get_next_count)
    : get_next_count_(get_next_count) {}
TestBackoff::~TestBackoff() {}

zx::duration TestBackoff::GetNext() {
  FXL_DCHECK(get_next_count_);
  (*get_next_count_)++;
  return zx::sec(0);
}

void TestBackoff::Reset() {}

TestWithPageStorage::TestWithPageStorage()
    : encryption_service_(dispatcher()) {}

TestWithPageStorage::~TestWithPageStorage() {}

fit::function<void(storage::Journal*)>
TestWithPageStorage::AddKeyValueToJournal(const std::string& key,
                                          std::string value) {
  return [this, key,
          value = std::move(value)](storage::Journal* journal) mutable {
    storage::Status status;
    storage::ObjectIdentifier object_identifier;
    bool called;
    page_storage()->AddObjectFromLocal(
        storage::DataSource::Create(std::move(value)),
        callback::Capture(callback::SetWhenCalled(&called), &status,
                          &object_identifier));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);

    journal->Put(key, object_identifier, storage::KeyPriority::EAGER,
                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
  };
}

fit::function<void(storage::Journal*)>
TestWithPageStorage::DeleteKeyFromJournal(const std::string& key) {
  return [this, key](storage::Journal* journal) {
    storage::Status status;
    bool called;
    journal->Delete(
        key, callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
  };
}

::testing::AssertionResult TestWithPageStorage::GetValue(
    storage::ObjectIdentifier object_identifier, std::string* value) {
  storage::Status status;
  std::unique_ptr<const storage::Object> object;
  bool called;
  page_storage()->GetObject(
      std::move(object_identifier), storage::PageStorage::Location::LOCAL,
      callback::Capture(callback::SetWhenCalled(&called), &status, &object));
  RunLoopUntilIdle();
  if (!called) {
    return ::testing::AssertionFailure()
           << "PageStorage::GetObject never called the callback.";
  }
  if (status != storage::Status::OK) {
    return ::testing::AssertionFailure()
           << "PageStorage::GetObject returned status: " << status;
  }

  fxl::StringView data;
  status = object->GetData(&data);
  if (status != storage::Status::OK) {
    return ::testing::AssertionFailure()
           << "Object::GetData returned status: " << status;
  }

  *value = data.ToString();
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult TestWithPageStorage::CreatePageStorage(
    std::unique_ptr<storage::PageStorage>* page_storage) {
  std::unique_ptr<storage::PageStorageImpl> local_page_storage =
      std::make_unique<storage::PageStorageImpl>(
          &environment_, &encryption_service_,
          ledger::DetachedPath(tmpfs_.root_fd()), kRootPageId.ToString());
  storage::Status status;
  bool called;
  local_page_storage->Init(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  if (!called) {
    return ::testing::AssertionFailure()
           << "PageStorage::Init never called the callback.";
  }

  if (status != storage::Status::OK) {
    return ::testing::AssertionFailure()
           << "PageStorageImpl::Init returned status: " << status;
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
