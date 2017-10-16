// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

namespace encryption {

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

std::string FakeEncryptionService::EncryptCommitSynchronous(
    convert::ExtendedStringView commit_storage) {
  return "_" + commit_storage.ToString() + "_";
}

std::string FakeEncryptionService::DecryptCommitSynchronous(
    convert::ExtendedStringView storage_bytes) {
  return storage_bytes.substr(1, storage_bytes.size() - 2).ToString();
}

}  // namespace encryption
