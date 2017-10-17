// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_

#include <functional>
#include <string>

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"

namespace encryption {

class FakeEncryptionService : public EncryptionService {
 public:
  FakeEncryptionService(fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~FakeEncryptionService() override;

  // EncryptionService:
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
      std::unique_ptr<const storage::Object> object,
      std::function<void(Status, std::string)> callback) override;
  void DecryptObject(
      storage::ObjectIdentifier object_identifier,
      std::string encrypted_data,
      std::function<void(Status, std::string)> callback) override;

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
      std::unique_ptr<const storage::Object> object);

  // Synchronously decrypts the object.
  std::string DecryptObjectSynchronous(
      storage::ObjectIdentifier object_identifier,
      std::string encrypted_data);

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_
