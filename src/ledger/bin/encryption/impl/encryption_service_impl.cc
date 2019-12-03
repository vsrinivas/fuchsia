// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/encryption_service_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <algorithm>

#include <flatbuffers/flatbuffers.h>

#include "src/ledger/bin/encryption/impl/encrypted_commit_generated.h"
#include "src/ledger/bin/encryption/impl/encrypted_entry_generated.h"
#include "src/ledger/bin/encryption/impl/remote_commit_id_generated.h"
#include "src/ledger/bin/encryption/primitives/encrypt.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/encryption/primitives/hmac.h"
#include "src/ledger/bin/encryption/primitives/kdf.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace encryption {
namespace {

// Version of the encryption scheme.
// This is used to check that the encryption scheme used in the data obtained from the cloud
// matches the one currently used.
// TODO(mariagl): Use this for the backward compatibility.
constexpr uint32_t kEncryptionVersion = 0;

// Entry id size in bytes.
constexpr size_t kEntryIdSize = 32u;

// Cache size values.
constexpr size_t kKeyIndexCacheSize = 10u;

// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidEncryptedCommitSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(storage_bytes.data()),
                                 storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}

// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted entry.
bool CheckValidEncryptedEntrySerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(storage_bytes.data()),
                                 storage_bytes.size());

  return VerifyEncryptedEntryStorageBuffer(verifier);
}

}  // namespace

EncryptionServiceImpl::EncryptionServiceImpl(ledger::Environment* environment,
                                             std::string namespace_id)
    : environment_(environment),
      namespace_id_(std::move(namespace_id)),
      key_service_(std::make_unique<KeyService>(environment_->dispatcher(), namespace_id_)),
      encryption_keys_(
          kKeyIndexCacheSize, Status::OK,
          [this](auto k, auto c) { key_service_->GetEncryptionKey(std::move(k), std::move(c)); }),
      remote_id_keys_(kKeyIndexCacheSize, Status::OK,
                      [this](auto k, auto c) {
                        key_service_->GetRemoteObjectIdKey(std::move(k), std::move(c));
                      }),
      chunking_key_(Status::OK, [this](auto c) { key_service_->GetChunkingKey(std::move(c)); }),
      page_id_key_(Status::OK, [this](auto c) { key_service_->GetPageIdKey(std::move(c)); }) {}

EncryptionServiceImpl::~EncryptionServiceImpl() = default;

storage::ObjectIdentifier EncryptionServiceImpl::MakeObjectIdentifier(
    storage::ObjectIdentifierFactory* factory, storage::ObjectDigest digest) {
  return factory->MakeObjectIdentifier(GetCurrentKeyIndex(), std::move(digest));
}

void EncryptionServiceImpl::EncryptCommit(std::string commit_storage,
                                          fit::function<void(Status, std::string)> callback) {
  size_t key_index = GetCurrentKeyIndex();

  Encrypt(
      key_index, std::move(commit_storage),
      [key_index, callback = std::move(callback)](Status status, std::string encrypted_storage) {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }

        flatbuffers::FlatBufferBuilder builder;

        auto storage = CreateEncryptedCommitStorage(
            builder, key_index, convert::ToFlatBufferVector(&builder, encrypted_storage));
        builder.Finish(storage);
        callback(Status::OK, convert::ToString(builder));
      });
}

std::string EncryptionServiceImpl::EncodeCommitId(std::string commit_id) {
  flatbuffers::FlatBufferBuilder builder;

  auto storage =
      CreateRemoteCommitId(builder, kEncryptionVersion,
                           convert::ToFlatBufferVector(&builder, SHA256WithLengthHash(commit_id)));
  builder.Finish(storage);
  return convert::ToString(builder);
}

void EncryptionServiceImpl::GetPageId(std::string page_name,
                                      fit::function<void(Status, std::string)> callback) {
  page_id_key_.Get([page_name = std::move(page_name), callback = std::move(callback)](
                       Status status, std::string page_id_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    callback(Status::OK, SHA256HMAC(page_id_key, page_name));
  });
}

void EncryptionServiceImpl::DecryptCommit(convert::ExtendedStringView storage_bytes,
                                          fit::function<void(Status, std::string)> callback) {
  if (!CheckValidEncryptedCommitSerialization(storage_bytes)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot decrypt commit.";
    callback(Status::INVALID_ARGUMENT, "");
    return;
  }

  const EncryptedCommitStorage* encrypted_commit_storage =
      GetEncryptedCommitStorage(storage_bytes.data());

  Decrypt(encrypted_commit_storage->key_index(),
          convert::ToString(encrypted_commit_storage->serialized_encrypted_commit_storage()),
          std::move(callback));
}

void EncryptionServiceImpl::EncryptEntryPayload(std::string entry_storage,
                                                fit::function<void(Status, std::string)> callback) {
  size_t key_index = GetCurrentKeyIndex();

  Encrypt(
      key_index, std::move(entry_storage),
      [key_index, callback = std::move(callback)](Status status, std::string encrypted_storage) {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }

        flatbuffers::FlatBufferBuilder builder;

        auto storage = CreateEncryptedEntryStorage(
            builder, key_index, convert::ToFlatBufferVector(&builder, encrypted_storage));
        builder.Finish(storage);
        callback(Status::OK, convert::ToString(builder));
      });
}

void EncryptionServiceImpl::DecryptEntryPayload(std::string encrypted_data,
                                                fit::function<void(Status, std::string)> callback) {
  if (!CheckValidEncryptedEntrySerialization(encrypted_data)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot decrypt the entry payload.";
    callback(Status::INVALID_ARGUMENT, "");
    return;
  }

  const EncryptedEntryStorage* encrypted_entry_storage =
      GetEncryptedEntryStorage(encrypted_data.data());

  Decrypt(encrypted_entry_storage->key_index(),
          convert::ToString(encrypted_entry_storage->serialized_encrypted_entry_storage()),
          std::move(callback));
}

void EncryptionServiceImpl::GetObjectName(storage::ObjectIdentifier object_identifier,
                                          fit::function<void(Status, std::string)> callback) {
  remote_id_keys_.Get(
      object_identifier.key_index(),
      [object_identifier = std::move(object_identifier), callback = std::move(callback)](
          Status status, std::string remote_object_id_key) {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }
        callback(Status::OK,
                 SHA256HMAC(remote_object_id_key, object_identifier.object_digest().Serialize()));
      });
}

void EncryptionServiceImpl::EncryptObject(storage::ObjectIdentifier object_identifier,
                                          fxl::StringView content,
                                          fit::function<void(Status, std::string)> callback) {
  Encrypt(object_identifier.key_index(), content.ToString(), std::move(callback));
}

void EncryptionServiceImpl::DecryptObject(storage::ObjectIdentifier object_identifier,
                                          std::string encrypted_data,
                                          fit::function<void(Status, std::string)> callback) {
  Decrypt(object_identifier.key_index(), std::move(encrypted_data), std::move(callback));
}

uint32_t EncryptionServiceImpl::GetCurrentKeyIndex() { return kDefaultKeyIndex; }

void EncryptionServiceImpl::Encrypt(size_t key_index, std::string data,
                                    fit::function<void(Status, std::string)> callback) {
  encryption_keys_.Get(
      key_index, [environment = environment_, data = std::move(data),
                  callback = std::move(callback)](Status status, const std::string& key) {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }
        std::string encrypted_data;
        if (!AES128GCMSIVEncrypt(environment->random(), key, data, &encrypted_data)) {
          callback(Status::INTERNAL_ERROR, "");
          return;
        }
        callback(Status::OK, std::move(encrypted_data));
      });
}

void EncryptionServiceImpl::Decrypt(size_t key_index, std::string encrypted_data,
                                    fit::function<void(Status, std::string)> callback) {
  encryption_keys_.Get(key_index,
                       [encrypted_data = std::move(encrypted_data), callback = std::move(callback)](
                           Status status, const std::string& key) {
                         if (status != Status::OK) {
                           callback(status, "");
                           return;
                         }
                         std::string data;
                         if (!AES128GCMSIVDecrypt(key, encrypted_data, &data)) {
                           callback(Status::INTERNAL_ERROR, "");
                           return;
                         }
                         callback(Status::OK, std::move(data));
                       });
}

void EncryptionServiceImpl::GetChunkingPermutation(
    fit::function<void(Status, fit::function<uint64_t(uint64_t)>)> callback) {
  chunking_key_.Get(
      [callback = std::move(callback)](Status status, const std::string& chunking_key) {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        const uint64_t chunking_permutation_key =
            *reinterpret_cast<const uint64_t*>(chunking_key.data());
        // TODO(35273): Use some other permutation.
        auto chunking_permutation = [chunking_permutation_key](uint64_t chunk_window_hash) {
          return chunk_window_hash ^ chunking_permutation_key;
        };
        callback(Status::OK, std::move(chunking_permutation));
      });
}

std::string EncryptionServiceImpl::GetEntryId() {
  std::string entry_id;
  entry_id.resize(kEntryIdSize);
  (environment_->random())->Draw(&entry_id[0], kEntryIdSize);
  return entry_id;
}

std::string EncryptionServiceImpl::GetEntryIdForMerge(fxl::StringView entry_name,
                                                      storage::CommitId left_parent_id,
                                                      storage::CommitId right_parent_id,
                                                      fxl::StringView operation_list) {
  // TODO(LE-827): Concatenation is ineffective; consider doing it once per commit.
  std::string input =
      fxl::Concatenate({entry_name, left_parent_id, right_parent_id, operation_list});
  std::string hash = SHA256WithLengthHash(input);
  hash.resize(kEntryIdSize);
  return hash;
}

bool EncryptionServiceImpl::IsSameVersion(convert::ExtendedStringView remote_commit_id) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(remote_commit_id.data()),
                                 remote_commit_id.size());

  if (!VerifyRemoteCommitIdBuffer(verifier)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot check the version.";
    return false;
  }

  const RemoteCommitId* data = GetRemoteCommitId(remote_commit_id.data());
  return data->version() == kEncryptionVersion;
}

}  // namespace encryption
