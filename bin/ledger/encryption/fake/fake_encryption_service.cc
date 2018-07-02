// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace encryption {

namespace {
std::string Encode(fxl::StringView content) {
  return "_" + content.ToString() + "_";
}

std::string Decode(fxl::StringView encrypted_content) {
  return encrypted_content.substr(1, encrypted_content.size() - 2).ToString();
}
}  // namespace

storage::ObjectIdentifier MakeDefaultObjectIdentifier(
    storage::ObjectDigest digest) {
  return {1u, 1u, std::move(digest)};
}

FakeEncryptionService::FakeEncryptionService(async_t* async) : async_(async) {}

FakeEncryptionService::~FakeEncryptionService() {}

storage::ObjectIdentifier FakeEncryptionService::MakeObjectIdentifier(
    storage::ObjectDigest digest) {
  return MakeDefaultObjectIdentifier(std::move(digest));
}

void FakeEncryptionService::EncryptCommit(
    std::string commit_storage,
    fit::function<void(Status, std::string)> callback) {
  std::string encrypted_commit = EncryptCommitSynchronous(commit_storage);
  async::PostTask(async_, [encrypted_commit = std::move(encrypted_commit),
                           callback = std::move(callback)]() mutable {
    callback(Status::OK, std::move(encrypted_commit));
  });
}

void FakeEncryptionService::DecryptCommit(
    convert::ExtendedStringView storage_bytes,
    fit::function<void(Status, std::string)> callback) {
  std::string commit = DecryptCommitSynchronous(storage_bytes);
  async::PostTask(async_, [commit = std::move(commit),
                           callback = std::move(callback)]() mutable {
    callback(Status::OK, std::move(commit));
  });
}

void FakeEncryptionService::GetObjectName(
    storage::ObjectIdentifier object_identifier,
    fit::function<void(Status, std::string)> callback) {
  std::string result = GetObjectNameSynchronous(object_identifier);
  async::PostTask(async_, [callback = std::move(callback),
                           result = std::move(result)]() mutable {
    callback(Status::OK, std::move(result));
  });
}

void FakeEncryptionService::EncryptObject(
    storage::ObjectIdentifier /*object_identifier*/, fsl::SizedVmo content,
    fit::function<void(Status, std::string)> callback) {
  std::string content_as_string;
  if (!fsl::StringFromVmo(content, &content_as_string)) {
    callback(Status::IO_ERROR, "");
    return;
  }
  std::string result = EncryptObjectSynchronous(content_as_string);
  async::PostTask(async_, [callback = std::move(callback),
                           result = std::move(result)]() mutable {
    callback(Status::OK, std::move(result));
  });
}

void FakeEncryptionService::DecryptObject(
    storage::ObjectIdentifier /*object_identifier*/, std::string encrypted_data,
    fit::function<void(Status, std::string)> callback) {
  std::string result = DecryptObjectSynchronous(encrypted_data);
  async::PostTask(async_, [callback = std::move(callback),
                           result = std::move(result)]() mutable {
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
    convert::ExtendedStringView object_content) {
  return Encode(object_content);
}

std::string FakeEncryptionService::DecryptObjectSynchronous(
    convert::ExtendedStringView encrypted_data) {
  return Decode(encrypted_data);
}

}  // namespace encryption
