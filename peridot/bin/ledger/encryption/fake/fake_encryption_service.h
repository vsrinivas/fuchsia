// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_

#include <functional>
#include <string>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/lib/convert/convert.h"

namespace encryption {

// Creates an |ObjectIdentifier| from an |ObjectDigest|.
//
// This method is always constructing the indentifier with the same key index
// and deletion scope.
storage::ObjectIdentifier MakeDefaultObjectIdentifier(
    storage::ObjectDigest digest);

class FakeEncryptionService : public EncryptionService {
 public:
  explicit FakeEncryptionService(async_dispatcher_t* dispatcher);
  ~FakeEncryptionService() override;

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
      storage::ObjectIdentifier object_identifier, fsl::SizedVmo content,
      fit::function<void(Status, std::string)> callback) override;
  void DecryptObject(
      storage::ObjectIdentifier object_identifier, std::string encrypted_data,
      fit::function<void(Status, std::string)> callback) override;

  // Synchronously encrypts the given commit.
  std::string EncryptCommitSynchronous(
      convert::ExtendedStringView commit_storage);

  // Synchronously decrypts the given commit.
  std::string DecryptCommitSynchronous(
      convert::ExtendedStringView storage_bytes);

  // Synchronously gets the object name.
  std::string GetObjectNameSynchronous(
      storage::ObjectIdentifier object_identifier);

  // Synchronously encrypts the object.
  std::string EncryptObjectSynchronous(
      convert::ExtendedStringView object_content);

  // Synchronously decrypts the object.
  std::string DecryptObjectSynchronous(
      convert::ExtendedStringView encrypted_data);

 private:
  async_dispatcher_t* dispatcher_;
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_
