// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
#define SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_

#include <functional>
#include <string>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/cache/lazy_value.h"
#include "src/ledger/bin/cache/lru_cache.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"

namespace encryption {

class EncryptionServiceImpl : public EncryptionService {
 public:
  EncryptionServiceImpl(ledger::Environment* environment,
                        std::string namespace_id);
  ~EncryptionServiceImpl() override;

  // EncryptionService:
  storage::ObjectIdentifier MakeObjectIdentifier(
      storage::ObjectDigest digest) override;
  void EncryptCommit(
      std::string commit_storage,
      fit::function<void(Status, std::string)> callback) override;
  void DecryptCommit(
      convert::ExtendedStringView storage_bytes,
      fit::function<void(Status, std::string)> callback) override;
  void GetObjectName(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(Status, std::string)> callback) override;
  void EncryptObject(
      storage::ObjectIdentifier object_identifier, fxl::StringView content,
      fit::function<void(Status, std::string)> callback) override;
  void DecryptObject(
      storage::ObjectIdentifier object_identifier, std::string encrypted_data,
      fit::function<void(Status, std::string)> callback) override;

 private:
  class KeyService;
  using DeletionScopeSeed = std::pair<size_t, std::string>;

  uint32_t GetCurrentKeyIndex();
  void GetReferenceKey(storage::ObjectIdentifier object_identifier,
                       fit::function<void(const std::string&)> callback);

  void Encrypt(size_t key_index, std::string data,
               fit::function<void(Status, std::string)> callback);
  void Decrypt(size_t key_index, std::string encrypted_data,
               fit::function<void(Status, std::string)> callback);

  void FetchMasterKey(size_t key_index,
                      fit::function<void(Status, std::string)> callback);
  void FetchNamespaceKey(size_t key_index,
                         fit::function<void(Status, std::string)> callback);
  void FetchReferenceKey(DeletionScopeSeed deletion_scope_seed,
                         fit::function<void(Status, std::string)> callback);

  ledger::Environment* const environment_;
  const std::string namespace_id_;
  std::unique_ptr<KeyService> key_service_;

  // Master keys indexed by key_index.
  cache::LRUCache<uint32_t, std::string, Status> master_keys_;
  // Namespace keys indexed by key_index.
  cache::LRUCache<uint32_t, std::string, Status> namespace_keys_;
  // Reference keys indexed by deletion scope seed.
  cache::LRUCache<DeletionScopeSeed, std::string, Status> reference_keys_;
};

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
