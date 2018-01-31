// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_

#include <functional>
#include <string>

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/cache/lazy_value.h"
#include "peridot/bin/ledger/cache/lru_cache.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/lib/callback/scoped_task_runner.h"
#include "peridot/lib/convert/convert.h"

namespace encryption {

class EncryptionServiceImpl : public EncryptionService {
 public:
  EncryptionServiceImpl(fxl::RefPtr<fxl::TaskRunner> task_runner,
                        std::string namespace_id);
  ~EncryptionServiceImpl() override;

  // EncryptionService:
  storage::ObjectIdentifier MakeObjectIdentifier(
      storage::ObjectDigest digest) override;
  void EncryptCommit(
      convert::ExtendedStringView commit_storage,
      std::function<void(Status, std::string)> callback) override;
  void DecryptCommit(
      convert::ExtendedStringView storage_bytes,
      std::function<void(Status, std::string)> callback) override;
  void GetObjectName(
      storage::ObjectIdentifier object_identifier,
      std::function<void(Status, std::string)> callback) override;
  void EncryptObject(
      storage::ObjectIdentifier object_identifier,
      fsl::SizedVmo content,
      std::function<void(Status, std::string)> callback) override;
  void DecryptObject(
      storage::ObjectIdentifier object_identifier,
      std::string encrypted_data,
      std::function<void(Status, std::string)> callback) override;

 private:
  class KeyService;

  uint32_t GetCurrentKeyIndex();
  void GetReferenceKey(uint32_t deletion_scope_id,
                       const std::string& digest,
                       const std::function<void(const std::string&)>& callback);

  void FetchMasterKey(std::function<void(Status, std::string)> callback);
  void FetchNamespaceKey(std::function<void(Status, std::string)> callback);
  void FetchReferenceKey(std::string deletion_scope_seed,
                         std::function<void(Status, std::string)> callback);

  const std::string namespace_id_;
  std::unique_ptr<KeyService> key_service_;

  // Lazy value for the master key.
  cache::LazyValue<std::string, Status> master_key_;
  // Lazy value for the namespace key.
  cache::LazyValue<std::string, Status> namespace_key_;
  // Reference keys indexed by deletion scope seed.
  cache::LRUCache<std::string, std::string, Status> reference_keys_;

  // This must be the last member of this class.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
