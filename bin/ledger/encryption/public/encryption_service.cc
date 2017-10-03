// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/public/encryption_service.h"

#include <flatbuffers/flatbuffers.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/encryption/public/constants.h"
#include "peridot/bin/ledger/encryption/public/encrypted_commit_generated.h"

namespace encryption {
namespace {
// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(storage_bytes.data()),
      storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}
}  // namespace

void EncryptCommit(convert::ExtendedStringView commit_storage,
                   std::function<void(Status, std::string)> callback) {
  flatbuffers::FlatBufferBuilder builder;

  auto storage = CreateEncryptedCommitStorage(
      builder, kDefaultKeyIndex, commit_storage.ToFlatBufferVector(&builder));
  builder.Finish(storage);

  std::string encrypted_storage(
      reinterpret_cast<const char*>(builder.GetBufferPointer()),
      builder.GetSize());

  // TODO(qsr): This is temporary until this method is transferred to a real
  // service. When done, it must not reference the task queue through a global
  // variable.
  fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([
    callback = std::move(callback),
    encrypted_storage = std::move(encrypted_storage)
  ]() mutable { callback(Status::OK, std::move(encrypted_storage)); });
}

void DecryptCommit(convert::ExtendedStringView storage_bytes,
                   std::function<void(Status, std::string)> callback) {
  if (!CheckValidSerialization(storage_bytes)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot decrypt commit.";
    callback(Status::INVALID_ARGUMENT, "");
  }

  const EncryptedCommitStorage* encrypted_commit_storage =
      GetEncryptedCommitStorage(storage_bytes.data());

  std::string commit_storage = convert::ToString(
      encrypted_commit_storage->serialized_encrypted_commit_storage());

  // TODO(qsr): This is temporary until this method is transferred to a real
  // service. When done, it must not reference the task queue through a global
  // variable.
  fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([
    callback = std::move(callback), commit_storage = std::move(commit_storage)
  ]() mutable { callback(Status::OK, std::move(commit_storage)); });
}

}  // namespace encryption
