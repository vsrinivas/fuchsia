// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include <flatbuffers/flatbuffers.h>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/encryption/impl/encrypted_commit_generated.h"

namespace encryption {
namespace {

// The default encryption values. Only used until real encryption is
// implemented: LE-286
//
// Use max_int32 for key_index as it will never be used in practice as it is not
// expected that any user will change its key 2^32 times.
constexpr uint32_t kDefaultKeyIndex = std::numeric_limits<uint32_t>::max();
// Use max_int32 - 1 for default deletion scoped id. max_int32 has a special
// meaning in the specification and is used to have per object deletion scope.
constexpr uint32_t kDefaultDeletionScopeId =
    std::numeric_limits<uint32_t>::max() - 1;

// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(storage_bytes.data()),
      storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}

}  // namespace

EncryptionServiceImpl::EncryptionServiceImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::string namespace_id)
    : task_runner_(std::move(task_runner)),
      namespace_id_(std::move(namespace_id)) {}

EncryptionServiceImpl::~EncryptionServiceImpl() {}

storage::ObjectIdentifier EncryptionServiceImpl::MakeObjectIdentifier(
    storage::ObjectDigest digest) {
  return {GetCurrentKeyIndex(), kDefaultDeletionScopeId, std::move(digest)};
}

void EncryptionServiceImpl::EncryptCommit(
    convert::ExtendedStringView commit_storage,
    std::function<void(Status, std::string)> callback) {
  flatbuffers::FlatBufferBuilder builder;

  auto storage =
      CreateEncryptedCommitStorage(builder, GetCurrentKeyIndex(),
                                   commit_storage.ToFlatBufferVector(&builder));
  builder.Finish(storage);

  std::string encrypted_storage(
      reinterpret_cast<const char*>(builder.GetBufferPointer()),
      builder.GetSize());

  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real encryption.
  task_runner_.PostTask(
      [callback = std::move(callback),
       encrypted_storage = std::move(encrypted_storage)]() mutable {
        callback(Status::OK, std::move(encrypted_storage));
      });
}

void EncryptionServiceImpl::DecryptCommit(
    convert::ExtendedStringView storage_bytes,
    std::function<void(Status, std::string)> callback) {
  if (!CheckValidSerialization(storage_bytes)) {
    FXL_LOG(WARNING) << "Received invalid data. Cannot decrypt commit.";
    callback(Status::INVALID_ARGUMENT, "");
    return;
  }

  const EncryptedCommitStorage* encrypted_commit_storage =
      GetEncryptedCommitStorage(storage_bytes.data());

  std::string commit_storage = convert::ToString(
      encrypted_commit_storage->serialized_encrypted_commit_storage());

  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real decryption.
  task_runner_.PostTask([callback = std::move(callback),
                         commit_storage = std::move(commit_storage)]() mutable {
    callback(Status::OK, std::move(commit_storage));
  });
}

void EncryptionServiceImpl::GetObjectName(
    storage::ObjectIdentifier object_identifier,
    std::function<void(Status, std::string)> callback) {
  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real hash.
  task_runner_.PostTask([callback = std::move(callback),
                         name = object_identifier.object_digest]() mutable {
    callback(Status::OK, "_" + name + "_");
  });
}

void EncryptionServiceImpl::EncryptObject(
    storage::ObjectIdentifier /*object_identifier*/,
    fsl::SizedVmo content,
    std::function<void(Status, std::string)> callback) {
  // TODO(qsr): Replace with real encryption.
  std::string data;
  if (!fsl::StringFromVmo(content, &data)) {
    callback(Status::IO_ERROR, "");
    return;
  }
  // Ensures the callback is asynchronous.
  task_runner_.PostTask(
      [callback = std::move(callback), data = std::move(data)]() mutable {
        callback(Status::OK, std::move(data));
      });
}

void EncryptionServiceImpl::DecryptObject(
    storage::ObjectIdentifier /*object_identifier*/,
    std::string encrypted_data,
    std::function<void(Status, std::string)> callback) {
  // Ensures the callback is asynchronous.
  // TODO(qsr): Replace with real decryption.
  task_runner_.PostTask([callback = std::move(callback),
                         encrypted_data = std::move(encrypted_data)]() mutable {
    callback(Status::OK, std::move(encrypted_data));
  });
}

uint32_t EncryptionServiceImpl::GetCurrentKeyIndex() {
  return kDefaultKeyIndex;
}

}  // namespace encryption
