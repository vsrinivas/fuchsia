// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_

#include <functional>
#include <string>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace encryption {

// Status of encryption operations.
enum class Status {
  OK,
  AUTH_ERROR,
  NETWORK_ERROR,
  INVALID_ARGUMENT,
  IO_ERROR,
  INTERNAL_ERROR,
};

// Returns whether the given |status| is a permanent error.
bool IsPermanentError(Status status);

// Handles all encryption for a page of the Ledger.
class EncryptionService {
 public:
  EncryptionService() {}
  virtual ~EncryptionService() {}

  // Construct the object identifier for the given digest, using the latest key
  // index and a default |deletion_scope_id|.
  // TODO(qsr): The user should have some control on the |deletion_scope_id| to
  // decide on the scope of deletion for objects.
  virtual storage::ObjectIdentifier MakeObjectIdentifier(
      storage::ObjectDigest digest) = 0;

  // Encrypts the given commit storage bytes for storing in the cloud.
  virtual void EncryptCommit(
      std::string commit_storage,
      fit::function<void(Status, std::string)> callback) = 0;

  // Decrypts the given encrypted commit storage bytes from the cloud.
  virtual void DecryptCommit(
      convert::ExtendedStringView storage_bytes,
      fit::function<void(Status, std::string)> callback) = 0;

  // Returns the obfuscated object name for the given identifier.
  //
  // This method is used to translate a local object identifier to the name that
  // is used to refer the object in the cloud provider.
  virtual void GetObjectName(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(Status, std::string)> callback) = 0;

  // Encrypts the given object.
  virtual void EncryptObject(
      storage::ObjectIdentifier object_identifier, fsl::SizedVmo content,
      fit::function<void(Status, std::string)> callback) = 0;

  // Decrypts the given object.
  virtual void DecryptObject(
      storage::ObjectIdentifier object_identifier, std::string encrypted_data,
      fit::function<void(Status, std::string)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EncryptionService);
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
