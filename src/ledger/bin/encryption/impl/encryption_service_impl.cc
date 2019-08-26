// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/encryption_service_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>

#include <algorithm>

#include <flatbuffers/flatbuffers.h>

#include "src/ledger/bin/encryption/impl/encrypted_commit_generated.h"
#include "src/ledger/bin/encryption/impl/remote_commit_id_generated.h"
#include "src/ledger/bin/encryption/primitives/encrypt.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/encryption/primitives/hmac.h"
#include "src/ledger/bin/encryption/primitives/kdf.h"
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

// The default encryption values. Only used until real encryption is
// implemented: LE-286

// Use max_int32 - 1 for default deletion scoped id. max_int32 has a special
// meaning in the specification and is used to have per object deletion scope.
constexpr uint32_t kDefaultDeletionScopeId = std::numeric_limits<uint32_t>::max() - 1;
// Special deletion scope id that produces a per-object deletion scope.
constexpr uint32_t kPerObjectDeletionScopedId = std::numeric_limits<uint32_t>::max();

// Entry id size in bytes.
constexpr size_t kEntryIdSize = 32u;

// Cache size values.
constexpr size_t kKeyIndexCacheSize = 10u;
constexpr size_t kReferenceKeysCacheSize = 10u;

// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(storage_bytes.data()),
                                 storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}

}  // namespace

EncryptionServiceImpl::EncryptionServiceImpl(ledger::Environment* environment,
                                             std::string namespace_id)
    : environment_(environment),
      namespace_id_(std::move(namespace_id)),
      key_service_(std::make_unique<KeyService>(environment_->dispatcher(), namespace_id_)),
      master_keys_(
          kKeyIndexCacheSize, Status::OK,
          [this](auto k, auto c) { key_service_->GetMasterKey(std::move(k), std::move(c)); }),
      namespace_keys_(kKeyIndexCacheSize, Status::OK,
                      [this](auto k, auto c) { FetchNamespaceKey(std::move(k), std::move(c)); }),
      reference_keys_(kReferenceKeysCacheSize, Status::OK,
                      [this](auto k, auto c) { FetchReferenceKey(std::move(k), std::move(c)); }),
      chunking_key_(Status::OK, [this](auto c) { key_service_->GetChunkingKey(std::move(c)); }) {}

EncryptionServiceImpl::~EncryptionServiceImpl() {}

storage::ObjectIdentifier EncryptionServiceImpl::MakeObjectIdentifier(
    storage::ObjectIdentifierFactory* factory, storage::ObjectDigest digest) {
  return factory->MakeObjectIdentifier(GetCurrentKeyIndex(), kDefaultDeletionScopeId,
                                       std::move(digest));
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
        callback(Status::OK, std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                                         builder.GetSize()));
      });
}

std::string EncryptionServiceImpl::EncodeCommitId(std::string commit_id) {
  flatbuffers::FlatBufferBuilder builder;

  auto storage =
      CreateRemoteCommitId(builder, kEncryptionVersion,
                           convert::ToFlatBufferVector(&builder, SHA256WithLengthHash(commit_id)));
  builder.Finish(storage);
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
}

void EncryptionServiceImpl::DecryptCommit(convert::ExtendedStringView storage_bytes,
                                          fit::function<void(Status, std::string)> callback) {
  if (!CheckValidSerialization(storage_bytes)) {
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

void EncryptionServiceImpl::GetObjectName(storage::ObjectIdentifier object_identifier,
                                          fit::function<void(Status, std::string)> callback) {
  GetReferenceKey(object_identifier, [object_identifier, callback = std::move(callback)](
                                         const std::string& reference_key) {
    callback(
        Status::OK,
        HMAC256KDF(fxl::Concatenate({reference_key, object_identifier.object_digest().Serialize()}),
                   kDerivedKeySize));
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

void EncryptionServiceImpl::GetReferenceKey(storage::ObjectIdentifier object_identifier,
                                            fit::function<void(const std::string&)> callback) {
  std::string deletion_scope_seed;
  if (object_identifier.deletion_scope_id() == kPerObjectDeletionScopedId) {
    deletion_scope_seed = object_identifier.object_digest().Serialize();
  } else {
    const uint32_t deletion_scope_id = object_identifier.deletion_scope_id();
    deletion_scope_seed =
        std::string(reinterpret_cast<const char*>(&deletion_scope_id), sizeof(deletion_scope_id));
  }
  DeletionScopeSeed seed = {object_identifier.key_index(), std::move(deletion_scope_seed)};
  reference_keys_.Get(seed, [callback = std::move(callback)](
                                Status status, const std::string& value) { callback(value); });
}

void EncryptionServiceImpl::Encrypt(size_t key_index, std::string data,
                                    fit::function<void(Status, std::string)> callback) {
  master_keys_.Get(key_index,
                   [environment = environment_, data = std::move(data),
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
  master_keys_.Get(key_index,
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

void EncryptionServiceImpl::FetchNamespaceKey(size_t key_index,
                                              fit::function<void(Status, std::string)> callback) {
  master_keys_.Get(key_index, [this, callback = std::move(callback)](
                                  Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    callback(Status::OK,
             HMAC256KDF(fxl::Concatenate({master_key, namespace_id_}), kDerivedKeySize));
  });
}

void EncryptionServiceImpl::FetchReferenceKey(DeletionScopeSeed deletion_scope_seed,
                                              fit::function<void(Status, std::string)> callback) {
  namespace_keys_.Get(
      deletion_scope_seed.first,
      [this, deletion_scope_seed = std::move(deletion_scope_seed), callback = std::move(callback)](
          Status status, const std::string& namespace_key) mutable {
        if (status != Status::OK) {
          callback(status, "");
          return;
        }
        key_service_->GetReferenceKey(
            namespace_id_,
            HMAC256KDF(fxl::Concatenate({namespace_key, deletion_scope_seed.second}),
                       kDerivedKeySize),
            [callback = std::move(callback)](std::string reference_key) {
              callback(Status::OK, std::move(reference_key));
            });
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
