// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_

#include <lib/fit/function.h>

#include <functional>
#include <string>

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/macros.h"

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
  EncryptionService() = default;
  virtual ~EncryptionService() = default;

  // Construct the object identifier for the given digest using the latest key
  // index.
  virtual storage::ObjectIdentifier MakeObjectIdentifier(storage::ObjectIdentifierFactory* factory,
                                                         storage::ObjectDigest digest) = 0;

  // Encrypts the given commit storage bytes for storing in the cloud.
  virtual void EncryptCommit(std::string commit_storage,
                             fit::function<void(Status, std::string)> callback) = 0;

  // Decrypts the given encrypted commit storage bytes from the cloud.
  virtual void DecryptCommit(convert::ExtendedStringView storage_bytes,
                             fit::function<void(Status, std::string)> callback) = 0;

  // Obfuscates the commit id by hashing it before sending it to the cloud.
  virtual std::string EncodeCommitId(std::string commit_id) = 0;

  // Checks whether the remote commit id mentions the currently used version.
  virtual bool IsSameVersion(convert::ExtendedStringView remote_commit_id) = 0;

  // Encrypts the entry payload (entry name, priority and reference) for storing in the cloud as
  // part of a diff.
  virtual void EncryptEntryPayload(std::string entry_payload_storage,
                                   fit::function<void(Status, std::string)> callback) = 0;

  // Decrypts the entry payload retrieved from the cloud.
  virtual void DecryptEntryPayload(std::string encrypted_data,
                                   fit::function<void(Status, std::string)> callback) = 0;

  // Returns the obfuscated object name for the given identifier.
  //
  // This method is used to translate a local object identifier to the name that
  // is used to refer the object in the cloud provider.
  virtual void GetObjectName(storage::ObjectIdentifier object_identifier,
                             fit::function<void(Status, std::string)> callback) = 0;

  // Returns an obfuscated page id to be used instead of page name in cloud sync.
  virtual void GetPageId(std::string page_name,
                         fit::function<void(Status, std::string)> callback) = 0;

  // Encrypts the given object.
  virtual void EncryptObject(storage::ObjectIdentifier object_identifier, fxl::StringView content,
                             fit::function<void(Status, std::string)> callback) = 0;

  // Decrypts the given object.
  virtual void DecryptObject(storage::ObjectIdentifier object_identifier,
                             std::string encrypted_data,
                             fit::function<void(Status, std::string)> callback) = 0;

  // Returns a permutation that can be applied to the window hash in the
  // chunking algorithm.
  virtual void GetChunkingPermutation(
      fit::function<void(Status, fit::function<uint64_t(uint64_t)>)> callback) = 0;

  // Returns an entry id that identifies an entry in a diff sent to the cloud.
  //
  // This version is used for non-merge commits.
  virtual std::string GetEntryId() = 0;

  // This version is used for merge commits to ensure different devices end up with the same entry
  // id for the same merge.
  virtual std::string GetEntryIdForMerge(fxl::StringView entry_name,
                                         storage::CommitId left_parent_id,
                                         storage::CommitId right_parent_id,
                                         fxl::StringView operation_list) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EncryptionService);
};

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PUBLIC_ENCRYPTION_SERVICE_H_
