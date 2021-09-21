// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy_pool.h>

#include <explicit-memory/bytes.h>

// See note in //zircon/third_party/ulib/boringssl/BUILD.gn
#define BORINGSSL_NO_CXX
#include <openssl/sha.h>

namespace crypto {

EntropyPool::EntropyPool(EntropyPool&& other) noexcept { (*this) = std::move(other); }

EntropyPool& EntropyPool::operator=(EntropyPool&& rhs) noexcept {
  contents_ = rhs.contents_;
  mandatory_memset(reinterpret_cast<void*>(&rhs), kShredValue, sizeof(EntropyPool));
  return *this;
}

EntropyPool::~EntropyPool() {
  static_assert(kContentSize == SHA256_DIGEST_LENGTH,
                "EntropyPool::contents size must match SHA256 digest length.");
  mandatory_memset(reinterpret_cast<void*>(this), kShredValue, sizeof(EntropyPool));
}

void EntropyPool::Add(ktl::span<const uint8_t> entropy) {
  ASSERT(entropy.size() <= kMaxEntropySize);

  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, entropy.data(), entropy.size());
  SHA256_Update(&ctx, contents_.data(), contents_.size());
  SHA256_Final(contents_.data(), &ctx);
}

}  // namespace crypto
