// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
#define SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <functional>
#include <string>

#include "src/ledger/bin/cache/lazy_value.h"
#include "src/ledger/bin/cache/lru_cache.h"
#include "src/ledger/bin/encryption/impl/key_service.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/lib/convert/convert.h"

namespace encryption {

class EncryptionServiceImpl : public EncryptionService {
 public:
  EncryptionServiceImpl(ledger::Environment* environment, std::string namespace_id);
  ~EncryptionServiceImpl() override;

  // EncryptionService:
  storage::ObjectIdentifier MakeObjectIdentifier(storage::ObjectIdentifierFactory* factory,
                                                 storage::ObjectDigest digest) override;
  void EncryptCommit(std::string commit_storage,
                     fit::function<void(Status, std::string)> callback) override;
  void DecryptCommit(convert::ExtendedStringView storage_bytes,
                     fit::function<void(Status, std::string)> callback) override;
  void EncryptEntryPayload(std::string entry_storage,
                           fit::function<void(Status, std::string)> callback) override;
  void DecryptEntryPayload(std::string encrypted_data,
                           fit::function<void(Status, std::string)> callback) override;
  void GetObjectName(storage::ObjectIdentifier object_identifier,
                     fit::function<void(Status, std::string)> callback) override;
  void EncryptObject(storage::ObjectIdentifier object_identifier, fxl::StringView content,
                     fit::function<void(Status, std::string)> callback) override;
  void DecryptObject(storage::ObjectIdentifier object_identifier, std::string encrypted_data,
                     fit::function<void(Status, std::string)> callback) override;
  void GetChunkingPermutation(
      fit::function<void(Status, fit::function<uint64_t(uint64_t)>)> callback) override;
  void GetPageId(std::string page_name, fit::function<void(Status, std::string)> callback) override;

  std::string GetEntryId() override;

  std::string GetEntryIdForMerge(fxl::StringView entry_name, storage::CommitId left_parent_id,
                                 storage::CommitId right_parent_id,
                                 fxl::StringView operation_list) override;

  std::string EncodeCommitId(std::string commit_id) override;

  bool IsSameVersion(convert::ExtendedStringView remote_commit_id) override;

 private:
  using DeletionScopeSeed = std::pair<size_t, std::string>;

  uint32_t GetCurrentKeyIndex();

  void Encrypt(size_t key_index, std::string data,
               fit::function<void(Status, std::string)> callback);
  void Decrypt(size_t key_index, std::string encrypted_data,
               fit::function<void(Status, std::string)> callback);

  ledger::Environment* const environment_;
  const std::string namespace_id_;
  std::unique_ptr<KeyService> key_service_;

  // Encryption keys indexed by key_index.
  cache::LRUCache<uint32_t, std::string, Status> encryption_keys_;
  // Remote id keys indexed by key_index.
  cache::LRUCache<uint32_t, std::string, Status> remote_id_keys_;

  // A key used for hash permutation in chunking.
  cache::LazyValue<std::string, Status> chunking_key_;
  // A key used for page id generation.
  cache::LazyValue<std::string, Status> page_id_key_;
};

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
