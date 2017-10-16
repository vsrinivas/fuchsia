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

  // Synchronously encrypts the given commit.
  std::string EncryptCommitSynchronous(
      convert::ExtendedStringView commit_storage);

  // Synchronously decrypts the given commit.
  std::string DecryptCommitSynchronous(
      convert::ExtendedStringView storage_bytes);

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_FAKE_FAKE_ENCRYPTION_SERVICE_H_
