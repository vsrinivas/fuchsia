// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/clocks/impl/device_id_manager_impl.h"

#include <cstdint>
#include <string>

#include "src/ledger/bin/public/status.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace clocks {
namespace {
constexpr size_t kFingerprintSize = 16;

constexpr absl::string_view kFingerprintKey = "clocks/fingerprint";
constexpr absl::string_view kFingerprintUnsyncedKey = "clocks/unsynced";
constexpr absl::string_view kCounterKey = "clocks/counter";

absl::string_view ToStringView(const uint64_t& counter) {
  return absl::string_view(reinterpret_cast<const char*>(&counter), sizeof(uint64_t));
}

uint64_t FromStringView(absl::string_view data) {
  FXL_DCHECK(data.size() == sizeof(uint64_t));
  uint64_t result;
  memcpy(&result, data.data(), sizeof(uint64_t));
  return result;
}

}  // namespace

DeviceIdManagerImpl::DeviceIdManagerImpl(ledger::Environment* environment,
                                         std::unique_ptr<storage::Db> db)
    : environment_(environment),
      initialization_completer_(environment_->dispatcher()),
      db_(std::move(db)) {}

DeviceIdManagerImpl::~DeviceIdManagerImpl() = default;

ledger::Status DeviceIdManagerImpl::Init(coroutine::CoroutineHandler* handler) {
  ledger::Status status = InternalInit(handler);
  initialization_completer_.Complete(status);
  return status;
}

ledger::Status DeviceIdManagerImpl::InternalInit(coroutine::CoroutineHandler* handler) {
  ledger::Status status = db_->Get(handler, kFingerprintKey, &fingerprint_);
  if (status == ledger::Status::INTERNAL_NOT_FOUND) {
    char fingerprint_array[kFingerprintSize];
    environment_->random()->Draw(fingerprint_array, kFingerprintSize);
    fingerprint_ = convert::ToHex(absl::string_view(fingerprint_array, kFingerprintSize));
    counter_ = 0;
    std::unique_ptr<storage::Db::Batch> batch;
    RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
    RETURN_ON_ERROR(batch->Put(handler, kFingerprintKey, fingerprint_));
    RETURN_ON_ERROR(batch->Put(handler, kFingerprintUnsyncedKey, ""));
    RETURN_ON_ERROR(batch->Put(handler, kCounterKey, ToStringView(counter_)));
    RETURN_ON_ERROR(batch->Execute(handler));
    upload_status_ = CloudUploadStatus::NOT_UPLOADED;
    return ledger::Status::OK;
  }
  RETURN_ON_ERROR(status);

  status = db_->HasKey(handler, kFingerprintUnsyncedKey);
  if (status == ledger::Status::INTERNAL_NOT_FOUND) {
    upload_status_ = CloudUploadStatus::UPLOADED;
  } else if (status == ledger::Status::OK) {
    upload_status_ = CloudUploadStatus::NOT_UPLOADED;
  } else {
    RETURN_ON_ERROR(status);
  }

  std::string counter_data;
  RETURN_ON_ERROR(db_->Get(handler, kCounterKey, &counter_data));
  counter_ = FromStringView(counter_data);

  return ledger::Status::OK;
}

ledger::Status DeviceIdManagerImpl::OnPageDeleted(coroutine::CoroutineHandler* handler) {
  RETURN_ON_ERROR(ledger::SyncWaitUntilDone(handler, &initialization_completer_));
  counter_++;
  std::unique_ptr<storage::Db::Batch> batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->Put(handler, kCounterKey, ToStringView(counter_)));
  return batch->Execute(handler);
}

ledger::Status DeviceIdManagerImpl::GetDeviceFingerprint(coroutine::CoroutineHandler* handler,
                                                         DeviceFingerprint* device_fingerprint,
                                                         CloudUploadStatus* status) {
  RETURN_ON_ERROR(SyncWaitUntilDone(handler, &initialization_completer_));
  *device_fingerprint = fingerprint_;
  *status = upload_status_;
  return ledger::Status::OK;
}

ledger::Status DeviceIdManagerImpl::SetDeviceFingerprintSynced(
    coroutine::CoroutineHandler* handler) {
  RETURN_ON_ERROR(SyncWaitUntilDone(handler, &initialization_completer_));
  upload_status_ = CloudUploadStatus::UPLOADED;
  std::unique_ptr<storage::Db::Batch> batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->Delete(handler, kFingerprintUnsyncedKey));
  return batch->Execute(handler);
}

ledger::Status DeviceIdManagerImpl::GetNewDeviceId(coroutine::CoroutineHandler* handler,
                                                   DeviceId* device_id) {
  RETURN_ON_ERROR(SyncWaitUntilDone(handler, &initialization_completer_));
  *device_id = DeviceId{fingerprint_, counter_};
  return ledger::Status::OK;
}

}  // namespace clocks
