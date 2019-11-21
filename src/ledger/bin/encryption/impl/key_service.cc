// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/impl/key_service.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <string>

#include "src/ledger/bin/encryption/primitives/kdf.h"
#include "src/lib/callback/scoped_callback.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"

namespace encryption {

namespace {

constexpr size_t kMasterKeysCacheSize = 10u;

}  // namespace

KeyService::KeyService(async_dispatcher_t* dispatcher, std::string namespace_id)
    : dispatcher_(dispatcher),
      namespace_id_(std::move(namespace_id)),
      master_keys_(kMasterKeysCacheSize, Status::OK,
                   [this](auto k, auto c) { GenerateMasterKey(std::move(k), std::move(c)); }),
      weak_factory_(this) {}

void KeyService::GetReferenceKey(const std::string& namespace_id,
                                 const std::string& reference_key_id,
                                 fit::function<void(const std::string&)> callback) {
  std::string result =
      HMAC256KDF(absl::StrCat(namespace_id, reference_key_id), kRandomlyGeneratedKeySize);
  async::PostTask(dispatcher_, callback::MakeScoped(weak_factory_.GetWeakPtr(),
                                                    [result = std::move(result),
                                                     callback = std::move(callback)]() mutable {
                                                      callback(result);
                                                    }));
}

void KeyService::GetWrappingKey(uint32_t key_index,
                                fit::function<void(Status, std::string)> callback) {
  master_keys_.Get(kDefaultKeyIndex, [this, callback = std::move(callback)](
                                         Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    std::string derived_key =
        HMAC256KDF(absl::StrCat(master_key, namespace_id_, "wrapping"), kDerivedKeySize);
    callback(Status::OK, derived_key);
  });
}

void KeyService::GetChunkingKey(fit::function<void(Status, std::string)> callback) {
  master_keys_.Get(kDefaultKeyIndex, [this, callback = std::move(callback)](
                                         Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    std::string derived_key =
        HMAC256KDF(absl::StrCat(master_key, namespace_id_, "chunking"), kChunkingKeySize);
    callback(Status::OK, derived_key);
  });
}

void KeyService::GetPageIdKey(fit::function<void(Status, std::string)> callback) {
  master_keys_.Get(kDefaultKeyIndex, [this, callback = std::move(callback)](
                                         Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    std::string derived_key =
        HMAC256KDF(absl::StrCat(master_key, namespace_id_, "page_id"), kDerivedKeySize);
    callback(Status::OK, derived_key);
  });
}

void KeyService::GetEncryptionKey(uint32_t key_index,
                                  fit::function<void(Status, std::string)> callback) {
  // TODO(12320): Derive this key from master key + shredding keys.
  master_keys_.Get(key_index, [this, callback = std::move(callback)](
                                  Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    std::string derived_key =
        HMAC256KDF(absl::StrCat(master_key, namespace_id_, "encryption"), 16u);
    callback(Status::OK, derived_key);
  });
}

void KeyService::GetRemoteObjectIdKey(uint32_t key_index,
                                      fit::function<void(Status, std::string)> callback) {
  // TODO(12320): Derive this key from master key + shredding keys.
  master_keys_.Get(key_index, [this, callback = std::move(callback)](
                                  Status status, const std::string& master_key) {
    if (status != Status::OK) {
      callback(status, "");
      return;
    }
    std::string derived_key =
        HMAC256KDF(absl::StrCat(master_key, namespace_id_, "remote_object_id"), kDerivedKeySize);
    callback(Status::OK, derived_key);
  });
}

void KeyService::GenerateMasterKey(uint32_t key_index,
                                   fit::function<void(Status, std::string)> callback) {
  async::PostTask(dispatcher_, callback::MakeScoped(weak_factory_.GetWeakPtr(),
                                                    [key_index, callback = std::move(callback)]() {
                                                      std::string master_key(16u, 0);
                                                      memcpy(&master_key[0], &key_index,
                                                             sizeof(key_index));
                                                      callback(Status::OK, std::move(master_key));
                                                    }));
}

}  // namespace encryption
