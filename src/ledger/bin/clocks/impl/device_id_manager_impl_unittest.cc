// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/clocks/impl/device_id_manager_impl.h"

#include <fuchsia/ledger/cpp/fidl.h>

#include <memory>
#include <random>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace clocks {
namespace {

// Wrapper allowing to open the same database multiple times.
class FakeDbWrapper : public storage::Db {
 public:
  explicit FakeDbWrapper(storage::Db* db) : db_(db) {}

  storage::Status StartBatch(coroutine::CoroutineHandler* handler,
                             std::unique_ptr<Batch>* batch) override {
    return db_->StartBatch(handler, batch);
  }

  storage::Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                      std::string* value) override {
    return db_->Get(handler, key, value);
  }

  storage::Status HasKey(coroutine::CoroutineHandler* handler,
                         convert::ExtendedStringView key) override {
    return db_->HasKey(handler, key);
  }

  storage::Status HasPrefix(coroutine::CoroutineHandler* handler,
                            convert::ExtendedStringView prefix) override {
    return db_->HasPrefix(handler, prefix);
  }

  storage::Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                            storage::ObjectIdentifier object_identifier,
                            std::unique_ptr<const storage::Piece>* piece) override {
    return db_->GetObject(handler, key, std::move(object_identifier), piece);
  }

  storage::Status GetByPrefix(coroutine::CoroutineHandler* handler,
                              convert::ExtendedStringView prefix,
                              std::vector<std::string>* key_suffixes) override {
    return db_->GetByPrefix(handler, prefix, key_suffixes);
  }

  storage::Status GetEntriesByPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::vector<std::pair<std::string, std::string>>* entries) override {
    return db_->GetEntriesByPrefix(handler, prefix, entries);
  }

  storage::Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<storage::Iterator<
          const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>* iterator)
      override {
    return db_->GetIteratorAtPrefix(handler, prefix, iterator);
  }

 private:
  storage::Db* const db_;
};

class DeviceIdManagerImplTest : public ledger::TestWithEnvironment {
 public:
  DeviceIdManagerImplTest()
      : db_(std::make_unique<storage::fake::FakeDb>(environment_.dispatcher())) {}
  ~DeviceIdManagerImplTest() override = default;

  std::unique_ptr<storage::Db> GetDb() { return std::make_unique<FakeDbWrapper>(db_.get()); }

 private:
  std::unique_ptr<storage::fake::FakeDb> db_;
};

TEST_F(DeviceIdManagerImplTest, OnPageEvictedNewDeviceId) {
  DeviceIdManagerImpl device_id_manager(&environment_, GetDb());
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    EXPECT_EQ(device_id_manager.Init(handler), ledger::Status::OK);

    DeviceId device_id_1;
    EXPECT_EQ(device_id_manager.GetNewDeviceId(handler, &device_id_1), ledger::Status::OK);
    DeviceId device_id_2;
    EXPECT_EQ(device_id_manager.GetNewDeviceId(handler, &device_id_2), ledger::Status::OK);
    EXPECT_EQ(device_id_1, device_id_2);

    EXPECT_EQ(device_id_manager.OnPageDeleted(handler), ledger::Status::OK);

    DeviceId device_id_3;
    EXPECT_EQ(device_id_manager.GetNewDeviceId(handler, &device_id_3), ledger::Status::OK);
    EXPECT_NE(device_id_3, device_id_1);
  });
}

TEST_F(DeviceIdManagerImplTest, GetSetDeviceFingerprint) {
  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    DeviceFingerprint fingerprint_1;

    {
      DeviceIdManagerImpl device_id_manager(&environment_, GetDb());
      EXPECT_EQ(device_id_manager.Init(handler), ledger::Status::OK);

      DeviceFingerprintManager::CloudUploadStatus upload_status_1;
      EXPECT_EQ(device_id_manager.GetDeviceFingerprint(handler, &fingerprint_1, &upload_status_1),
                ledger::Status::OK);

      EXPECT_EQ(upload_status_1, DeviceFingerprintManager::CloudUploadStatus::NOT_UPLOADED);
    }

    // New manager, same database: fingerprint is the same and not uploaded.
    {
      DeviceIdManagerImpl device_id_manager(&environment_, GetDb());
      EXPECT_EQ(device_id_manager.Init(handler), ledger::Status::OK);

      DeviceFingerprint fingerprint_2;
      DeviceFingerprintManager::CloudUploadStatus upload_status_2;
      EXPECT_EQ(device_id_manager.GetDeviceFingerprint(handler, &fingerprint_2, &upload_status_2),
                ledger::Status::OK);

      EXPECT_EQ(upload_status_2, DeviceFingerprintManager::CloudUploadStatus::NOT_UPLOADED);
      EXPECT_EQ(fingerprint_1, fingerprint_2);

      EXPECT_EQ(device_id_manager.SetDeviceFingerprintSynced(handler), ledger::Status::OK);

      DeviceFingerprint fingerprint_2_bis;
      DeviceFingerprintManager::CloudUploadStatus upload_status_2_bis;
      EXPECT_EQ(
          device_id_manager.GetDeviceFingerprint(handler, &fingerprint_2_bis, &upload_status_2_bis),
          ledger::Status::OK);
      EXPECT_EQ(upload_status_2_bis, DeviceFingerprintManager::CloudUploadStatus::UPLOADED);
      EXPECT_EQ(fingerprint_2, fingerprint_2_bis);
    }

    // New manager, same database: fingerprint is the same, and still uploaded.
    DeviceFingerprint fingerprint_3;
    {
      DeviceIdManagerImpl device_id_manager(&environment_, GetDb());
      EXPECT_EQ(device_id_manager.Init(handler), ledger::Status::OK);

      DeviceFingerprintManager::CloudUploadStatus upload_status_3;
      EXPECT_EQ(device_id_manager.GetDeviceFingerprint(handler, &fingerprint_3, &upload_status_3),
                ledger::Status::OK);

      EXPECT_EQ(upload_status_3, DeviceFingerprintManager::CloudUploadStatus::UPLOADED);
      EXPECT_EQ(fingerprint_3, fingerprint_1);
    }
  });
}

}  // namespace
}  // namespace clocks
