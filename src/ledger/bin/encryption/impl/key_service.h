// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_IMPL_KEY_SERVICE_H_
#define SRC_LEDGER_BIN_ENCRYPTION_IMPL_KEY_SERVICE_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <string>

#include "src/ledger/bin/cache/lru_cache.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace encryption {

// The default encryption values. Only used until real encryption is
// implemented. BUG: 12209
//
// Use max_int32 for key_index as it will never be used in practice as it is not
// expected that any user will change its key 2^32 times.
constexpr uint32_t kDefaultKeyIndex = std::numeric_limits<uint32_t>::max();

// Size of keys. Key must have 128 bits of entropy. Randomly generated keys can
// be 128 bits long, but derived ones need to be twice as big because of the
// birthday paradox.
// Size of the randomly generated key.
constexpr size_t kRandomlyGeneratedKeySize = 16u;
// Size of the derived keys.
constexpr size_t kDerivedKeySize = 32u;
// Size of the key used to generate a chunking permutation.
constexpr size_t kChunkingKeySize = 8u;

// Fake implementation of a key service for the Ledger.
//
// This implementation generate fake keys and will need to be replaced by a
// real component. BUG: 12165, 12320
class KeyService {
 public:
  explicit KeyService(async_dispatcher_t* dispatcher, std::string namespace_id);

  // Retrieves the reference key associated to the given namespace and reference
  // key. If the id is not yet associated with a reference key, generates a new
  // one and associates it with the id before returning.
  void GetReferenceKey(const std::string& namespace_id, const std::string& reference_key_id,
                       fit::function<void(const std::string&)> callback);

  // Retreives a static key for a chunking permutation.
  // TODO(35273): Should depend on the page name.
  void GetChunkingKey(fit::function<void(Status, std::string)> callback);

  // Retrieves a static key for generating a page id.
  void GetPageIdKey(fit::function<void(Status, std::string)> callback);

  // Retrieves a wrapping key. This key is used to encrypt and decrypt the shredding keys while
  // exchanging them via the Shredding Key Service.
  // TODO(12320) Use it with a fake ShreddingKeyService.
  void GetWrappingKey(uint32_t key_index, fit::function<void(Status, std::string)> callback);

  // Retrieves the encryption key.
  void GetEncryptionKey(uint32_t key_index, fit::function<void(Status, std::string)> callback);

  // Retrieves a key for generating remore object ids.
  void GetRemoteObjectIdKey(uint32_t key_index, fit::function<void(Status, std::string)> callback);

 private:
  void GenerateMasterKey(uint32_t key_index, fit::function<void(Status, std::string)> callback);

  async_dispatcher_t* const dispatcher_;

  // Id of the namespace for which the keys are generated.
  const std::string namespace_id_;

  // Master keys indexed by key_index.
  cache::LRUCache<uint32_t, std::string, Status> master_keys_;

  ledger::WeakPtrFactory<KeyService> weak_factory_;
};

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_IMPL_KEY_SERVICE_H_
