// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/impl/encryption_service_impl.h"

#include <flatbuffers/flatbuffers.h>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/encryption/impl/encrypted_commit_generated.h"
#include "peridot/bin/ledger/encryption/primitives/kdf.h"

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
// Special deletion scope id that produces a per-object deletion scope.
constexpr uint32_t kPerObjectDeletionScopedId =
    std::numeric_limits<uint32_t>::max();

// Size of keys. Key must have 128 bits of entropy. Randomly generated keys can
// be 128 bits long, but derived ones need to be twice as big because of the
// birthday paradox.
// Size of the randomly generated key.
constexpr size_t kRandomlyGeneratedKeySize = 16u;
// Size of the derived keys.
constexpr size_t kDerivedKeySize = 32u;

// Checks whether the given |storage_bytes| are a valid serialization of an
// encrypted commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(storage_bytes.data()),
      storage_bytes.size());

  return VerifyEncryptedCommitStorageBuffer(verifier);
}

}  // namespace

// Fake implementation of a key service for the Ledger.
//
// This implementation generate fake keys and will need to be replaced by a
// real component.
class EncryptionServiceImpl::KeyService {
 public:
  explicit KeyService(fxl::RefPtr<fxl::TaskRunner> task_runner)
      : task_runner_(std::move(task_runner)) {}

  // Retrieves the master key.
  void GetMasterKey(const std::function<void(const std::string&)>& callback) {
    if (!master_key_.empty()) {
      callback(master_key_);
      return;
    }
    master_key_callbacks_.push_back(callback);
    if (master_key_callbacks_.size() == 1) {
      task_runner_.PostTask([this]() {
        master_key_ = std::string(16u, 0);
        auto callbacks = std::move(master_key_callbacks_);
        master_key_callbacks_.clear();
        for (const auto& callback : callbacks) {
          callback(master_key_);
        }
      });
    }
  }

  // Retrieves the reference key associated to the given namespace and reference
  // key. If the id is not yet associated with a reference key, generates a new
  // one and associates it with the id before returning.
  void GetReferenceKey(const std::string& namespace_id,
                       const std::string& reference_key_id,
                       std::function<void(const std::string&)> callback) {
    std::string result =
        HMAC256KDF(fxl::Concatenate({namespace_id, reference_key_id}),
                   kRandomlyGeneratedKeySize);
    task_runner_.PostTask(
        [result = std::move(result), callback = std::move(callback)]() mutable {
          callback(result);
        });
  }

 private:
  std::vector<std::function<void(const std::string&)>> master_key_callbacks_;
  std::string master_key_;

  // This must be the last member of this class.
  callback::ScopedTaskRunner task_runner_;
};

EncryptionServiceImpl::EncryptionServiceImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::string namespace_id)
    : namespace_id_(std::move(namespace_id)),
      key_service_(std::make_unique<KeyService>(task_runner)),
      task_runner_(std::move(task_runner)) {}

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
  GetReferenceKey(
      object_identifier.deletion_scope_id, object_identifier.object_digest,
      [object_identifier,
       callback = std::move(callback)](const std::string& reference_key) {
        callback(Status::OK,
                 HMAC256KDF(fxl::Concatenate({reference_key,
                                              object_identifier.object_digest}),
                            kDerivedKeySize));
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

void EncryptionServiceImpl::GetNamespaceKey(
    const std::function<void(const std::string&)>& callback) {
  if (!namespace_key_.empty()) {
    callback(namespace_key_);
    return;
  }
  namespace_key_callbacks_.push_back(callback);
  if (namespace_key_callbacks_.size() == 1u) {
    key_service_->GetMasterKey([this](const std::string& master_key) {
      namespace_key_ = HMAC256KDF(fxl::Concatenate({master_key, namespace_id_}),
                                  kDerivedKeySize);
      auto callbacks = std::move(namespace_key_callbacks_);
      namespace_key_callbacks_.clear();
      for (const auto& callback : callbacks) {
        callback(namespace_key_);
      }
    });
  }
}

void EncryptionServiceImpl::GetReferenceKey(
    uint32_t deletion_scope_id,
    const std::string& digest,
    const std::function<void(const std::string&)>& callback) {
  std::string deletion_scope_seed;
  if (deletion_scope_id == kPerObjectDeletionScopedId) {
    deletion_scope_seed = digest;
  } else {
    deletion_scope_seed = std::string(
        reinterpret_cast<char*>(&deletion_scope_id), sizeof(deletion_scope_id));
  }
  // TODO(qsr): Cache the result using deletion_scope_seed as the key.
  GetNamespaceKey([this, deletion_scope_seed, callback = callback](
                      const std::string& namespace_key) mutable {
    key_service_->GetReferenceKey(
        namespace_id_,
        HMAC256KDF(fxl::Concatenate({namespace_key, deletion_scope_seed}),
                   kDerivedKeySize),
        std::move(callback));
  });
}

}  // namespace encryption
