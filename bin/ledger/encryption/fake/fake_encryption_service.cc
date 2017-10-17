// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/concatenate.h"

namespace encryption {

namespace {
std::string Encode(fxl::StringView content) {
  return "_" + content.ToString() + "_";
}

std::string Decode(fxl::StringView encrypted_content) {
  return encrypted_content.substr(1, encrypted_content.size() - 2).ToString();
}
}  // namespace

FakeEncryptionService::FakeEncryptionService(
    fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

FakeEncryptionService::~FakeEncryptionService() {}

void FakeEncryptionService::EncryptCommit(
    convert::ExtendedStringView commit_storage,
    std::function<void(Status, std::string)> callback) {
  std::string encrypted_commit = EncryptCommitSynchronous(commit_storage);
  task_runner_->PostTask([encrypted_commit = std::move(encrypted_commit),
                          callback = std::move(callback)]() mutable {
    callback(Status::OK, std::move(encrypted_commit));
  });
}

void FakeEncryptionService::DecryptCommit(
    convert::ExtendedStringView storage_bytes,
    std::function<void(Status, std::string)> callback) {
  std::string commit = DecryptCommitSynchronous(storage_bytes);
  task_runner_->PostTask(
      [commit = std::move(commit), callback = std::move(callback)]() mutable {
        callback(Status::OK, std::move(commit));
      });
}

void FakeEncryptionService::GetObjectName(
    storage::ObjectIdentifier object_identifier,
    std::function<void(Status, std::string)> callback) {
  std::string result = GetObjectNameSynchronous(object_identifier);
  task_runner_->PostTask(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(Status::OK, std::move(result));
      });
}

void FakeEncryptionService::EncryptObject(
    std::unique_ptr<const storage::Object> object,
    std::function<void(Status, std::string)> callback) {
  std::string result = EncryptObjectSynchronous(std::move(object));
  task_runner_->PostTask(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(Status::OK, std::move(result));
      });
}

void FakeEncryptionService::DecryptObject(
    storage::ObjectIdentifier object_identifier,
    std::string encrypted_data,
    std::function<void(Status, std::string)> callback) {
  std::string result = DecryptObjectSynchronous(std::move(object_identifier),
                                                std::move(encrypted_data));
  task_runner_->PostTask(
      [callback = std::move(callback), result = std::move(result)]() mutable {
        callback(Status::OK, std::move(result));
      });
}

std::string FakeEncryptionService::EncryptCommitSynchronous(
    convert::ExtendedStringView commit_storage) {
  return Encode(commit_storage);
}

std::string FakeEncryptionService::DecryptCommitSynchronous(
    convert::ExtendedStringView storage_bytes) {
  return Decode(storage_bytes);
}

std::string FakeEncryptionService::GetObjectNameSynchronous(
    storage::ObjectIdentifier object_identifier) {
  return Encode(object_identifier.object_digest);
}

std::string FakeEncryptionService::EncryptObjectSynchronous(
    std::unique_ptr<const storage::Object> object) {
  fxl::StringView data;
  storage::Status status = object->GetData(&data);
  if (status != storage::Status::OK) {
    return "";
  }
  return Encode(data);
}

std::string FakeEncryptionService::DecryptObjectSynchronous(
    storage::ObjectIdentifier object_identifier,
    std::string encrypted_data) {
  return Decode(encrypted_data);
}

}  // namespace encryption
