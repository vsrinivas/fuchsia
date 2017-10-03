// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_

#include <functional>
#include <string>

#include "peridot/bin/ledger/convert/convert.h"

namespace encryption {

// Status of encryption operations.
enum class Status {
  OK,
  NETWORK_ERROR,
  INVALID_ARGUMENT,
  INTERNAL_ERROR,
};

// Encrypt the given commit storage bytes for storing in the cloud.
void EncryptCommit(convert::ExtendedStringView commit_storage,
                   std::function<void(Status, std::string)> callback);

// Decrypt the given encrypted commit storage bytes from the cloud.
void DecryptCommit(convert::ExtendedStringView encrypted_commit_storage,
                   std::function<void(Status, std::string)> callback);

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
