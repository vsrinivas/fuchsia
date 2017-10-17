// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include <flatbuffers/flatbuffers.h>

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/encryption/impl/encrypted_commit_generated.h"
#include "peridot/bin/ledger/encryption/public/constants.h"

namespace encryption {
namespace {
// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(storage_bytes.data()),
      storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}

Status ToEncryptionStatus(storage::Status status) {
  if (status == storage::Status::OK) {
    return Status::OK;
  }

  return Status::INTERNAL_ERROR;
}
}  // namespace

EncryptionServiceImpl::EncryptionServiceImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

EncryptionServiceImpl::~EncryptionServiceImpl() {}

void EncryptionServiceImpl::EncryptCommit(
    convert::ExtendedStringView commit_storage,
    std::function<void(Status, std::string)> callback) {
  flatbuffers::FlatBufferBuilder builder;

  auto storage = CreateEncryptedCommitStorage(
      builder, kDefaultKeyIndex, commit_storage.ToFlatBufferVector(&builder));
  builder.Finish(storage);

  std::string encrypted_storage(
      reinterpret_cast<const char*>(builder.GetBufferPointer()),
      builder.GetSize());

  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real encryption.
  task_runner_.PostTask(
      [callback = std::move(callback),
       encrypted_storage = std::move(encrypted_storage)]() mutable {
        callback(Status::OK, std::move(encrypted_storage));
      });
}

void EncryptionServiceImpl::DecryptCommit(
    convert::ExtendedStringView storage_bytes,
    std::function<void(Status, std::string)> callback) {
  if (!CheckValidSerialization(storage_bytes)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot decrypt commit.";
    callback(Status::INVALID_ARGUMENT, "");
    return;
  }

  const EncryptedCommitStorage* encrypted_commit_storage =
      GetEncryptedCommitStorage(storage_bytes.data());

  std::string commit_storage = convert::ToString(
      encrypted_commit_storage->serialized_encrypted_commit_storage());

  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real decryption.
  task_runner_.PostTask([callback = std::move(callback),
                         commit_storage = std::move(commit_storage)]() mutable {
    callback(Status::OK, std::move(commit_storage));
  });
}

void EncryptionServiceImpl::GetObjectName(
    storage::ObjectIdentifier object_identifier,
    std::function<void(Status, std::string)> callback) {
  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real hash.
  task_runner_.PostTask([callback = std::move(callback),
                         name = object_identifier.object_digest]() mutable {
    callback(Status::OK, std::move(name));
  });
}

void EncryptionServiceImpl::EncryptObject(
    std::unique_ptr<const storage::Object> object,
    std::function<void(Status, std::string)> callback) {
  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real encryption.
  task_runner_.PostTask(fxl::MakeCopyable(
      [callback = std::move(callback), object = std::move(object)]() mutable {
        fxl::StringView data;
        Status status = ToEncryptionStatus(object->GetData(&data));
        callback(status, data.ToString());
      }));
}

void EncryptionServiceImpl::DecryptObject(
    storage::ObjectIdentifier object_identifier,
    std::string encrypted_data,
    std::function<void(Status, std::string)> callback) {
  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real decryption.
  task_runner_.PostTask([callback = std::move(callback),
                         encrypted_data = std::move(encrypted_data)]() mutable {
    callback(Status::OK, encrypted_data);
  });
}

}  // namespace encryption
