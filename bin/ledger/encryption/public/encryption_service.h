// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_

#include <functional>
#include <string>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace encryption {

// Status of encryption operations.
enum class Status {
  OK,
  NETWORK_ERROR,
  INVALID_ARGUMENT,
  INTERNAL_ERROR,
};

// Handles all encryption for a page of the Ledger.
class EncryptionService {
 public:
  EncryptionService() {}
  virtual ~EncryptionService() {}

  // Encrypts the given commit storage bytes for storing in the cloud.
  virtual void EncryptCommit(
      convert::ExtendedStringView commit_storage,
      std::function<void(Status, std::string)> callback) = 0;

  // Decrypts the given encrypted commit storage bytes from the cloud.
  virtual void DecryptCommit(
      convert::ExtendedStringView storage_bytes,
      std::function<void(Status, std::string)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EncryptionService);
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
