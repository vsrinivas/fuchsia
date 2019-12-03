// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <string>

#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace encryption {

namespace {
std::string Encode(fxl::StringView content) { return "_" + content.ToString() + "_"; }

std::string Decode(fxl::StringView encrypted_content) {
  return encrypted_content.substr(1, encrypted_content.size() - 2).ToString();
}

// Entry id size in bytes.
constexpr size_t kEntryIdSize = 32u;

}  // namespace

storage::ObjectIdentifier MakeDefaultObjectIdentifier(storage::ObjectIdentifierFactory* factory,
                                                      storage::ObjectDigest digest) {
  return factory->MakeObjectIdentifier(1u, std::move(digest));
}

uint64_t DefaultPermutation(uint64_t chunk_window_hash) { return 1 + chunk_window_hash; }

FakeEncryptionService::FakeEncryptionService(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

FakeEncryptionService::~FakeEncryptionService() = default;

storage::ObjectIdentifier FakeEncryptionService::MakeObjectIdentifier(
    storage::ObjectIdentifierFactory* factory, storage::ObjectDigest digest) {
  return MakeDefaultObjectIdentifier(factory, std::move(digest));
}

void FakeEncryptionService::EncryptCommit(std::string commit_storage,
                                          fit::function<void(Status, std::string)> callback) {
  std::string encrypted_commit = EncryptCommitSynchronous(commit_storage);
  async::PostTask(dispatcher_, [encrypted_commit = std::move(encrypted_commit),
                                callback = std::move(callback)]() mutable {
    callback(Status::OK, std::move(encrypted_commit));
  });
}

std::string FakeEncryptionService::EncodeCommitId(std::string commit_id) {
  return Encode(commit_id);
}

bool FakeEncryptionService::IsSameVersion(convert::ExtendedStringView remote_commit_id) {
  return true;
}

void FakeEncryptionService::DecryptCommit(convert::ExtendedStringView storage_bytes,
                                          fit::function<void(Status, std::string)> callback) {
  std::string commit = DecryptCommitSynchronous(storage_bytes);
  async::PostTask(dispatcher_,
                  [commit = std::move(commit), callback = std::move(callback)]() mutable {
                    callback(Status::OK, std::move(commit));
                  });
}

void FakeEncryptionService::EncryptEntryPayload(std::string entry_storage,
                                                fit::function<void(Status, std::string)> callback) {
  std::string encrypted_entry = EncryptEntryPayloadSynchronous(entry_storage);
  async::PostTask(dispatcher_, [encrypted_entry = std::move(encrypted_entry),
                                callback = std::move(callback)]() mutable {
    callback(Status::OK, std::move(encrypted_entry));
  });
}

void FakeEncryptionService::DecryptEntryPayload(std::string encrypted_data,
                                                fit::function<void(Status, std::string)> callback) {
  std::string entry = DecryptEntryPayloadSynchronous(encrypted_data);
  async::PostTask(dispatcher_,
                  [entry = std::move(entry), callback = std::move(callback)]() mutable {
                    callback(Status::OK, std::move(entry));
                  });
}

void FakeEncryptionService::GetObjectName(storage::ObjectIdentifier object_identifier,
                                          fit::function<void(Status, std::string)> callback) {
  std::string result = GetObjectNameSynchronous(object_identifier);
  async::PostTask(dispatcher_,
                  [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(Status::OK, std::move(result));
                  });
}

void FakeEncryptionService::GetPageId(std::string page_name,
                                      fit::function<void(Status, std::string)> callback) {
  std::string result = GetPageIdSynchronous(page_name);
  async::PostTask(dispatcher_,
                  [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(Status::OK, std::move(result));
                  });
}

void FakeEncryptionService::EncryptObject(storage::ObjectIdentifier /*object_identifier*/,
                                          fxl::StringView content,
                                          fit::function<void(Status, std::string)> callback) {
  std::string result = EncryptObjectSynchronous(content.ToString());
  async::PostTask(dispatcher_,
                  [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(Status::OK, std::move(result));
                  });
}

void FakeEncryptionService::DecryptObject(storage::ObjectIdentifier /*object_identifier*/,
                                          std::string encrypted_data,
                                          fit::function<void(Status, std::string)> callback) {
  std::string result = DecryptObjectSynchronous(encrypted_data);
  async::PostTask(dispatcher_,
                  [callback = std::move(callback), result = std::move(result)]() mutable {
                    callback(Status::OK, std::move(result));
                  });
}

void FakeEncryptionService::GetChunkingPermutation(
    fit::function<void(Status, fit::function<uint64_t(uint64_t)>)> callback) {
  auto chunking_permutation = [](uint64_t chunk_window_hash) { return chunk_window_hash + 1; };
  callback(Status::OK, std::move(chunking_permutation));
}

std::string FakeEncryptionService::GetEntryId() {
  std::string counter_str = std::to_string(entry_id_counter_++);
  std::string padding(kEntryIdSize - counter_str.size(), 0);
  return fxl::Concatenate({std::move(padding), std::move(counter_str)});
}

std::string FakeEncryptionService::GetEntryIdForMerge(fxl::StringView entry_name,
                                                      storage::CommitId left_parent_id,
                                                      storage::CommitId right_parent_id,
                                                      fxl::StringView operation_list) {
  std::string inputs =
      fxl::Concatenate({entry_name, left_parent_id, right_parent_id, operation_list});
  if (merge_entry_ids_.find(inputs) == merge_entry_ids_.end()) {
    merge_entry_ids_[inputs] = GetEntryId();
  }
  return merge_entry_ids_[inputs];
}

std::string FakeEncryptionService::EncryptCommitSynchronous(
    convert::ExtendedStringView commit_storage) {
  return Encode(commit_storage);
}

std::string FakeEncryptionService::DecryptCommitSynchronous(
    convert::ExtendedStringView storage_bytes) {
  return Decode(storage_bytes);
}

std::string FakeEncryptionService::EncryptEntryPayloadSynchronous(
    convert::ExtendedStringView entry_storage) {
  return Encode(entry_storage);
}

std::string FakeEncryptionService::DecryptEntryPayloadSynchronous(
    convert::ExtendedStringView encrypted_data) {
  return Decode(encrypted_data);
}

std::string FakeEncryptionService::GetObjectNameSynchronous(
    storage::ObjectIdentifier object_identifier) {
  return Encode(object_identifier.object_digest().Serialize());
}

std::string FakeEncryptionService::GetPageIdSynchronous(convert::ExtendedStringView page_name) {
  return Encode(page_name);
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
