// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_ENTROPY_POOL_H_
#define ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_ENTROPY_POOL_H_

#include <stdint.h>

#include <ktl/array.h>
#include <ktl/span.h>

namespace crypto {

// Represents a collection of entropy from multiple sources.
struct EntropyPool {
 public:
  // Maximum allowed size for any collected entropy to be added into the pool.
  static constexpr uint64_t kMaxEntropySize = 1ull << 30;

  // Shred value to use for |mandatory_memset|
  static constexpr uint8_t kShredValue = 0b11100110;

  constexpr EntropyPool() = default;
  EntropyPool(const EntropyPool&) = delete;
  EntropyPool(EntropyPool&&) noexcept;
  EntropyPool& operator=(const EntropyPool&) = delete;
  EntropyPool& operator=(EntropyPool&&) noexcept;
  ~EntropyPool();

  // Adds |entropy| into the |pool|, collecting |entropy.size()| bits of entropy.
  void Add(ktl::span<const uint8_t> entropy);

  // Creates a copy of the current state of |EntropyPool|.
  EntropyPool Clone() const {
    EntropyPool pool;
    memcpy(pool.contents_.data(), contents_.data(), contents_.size());
    return pool;
  }

  // Returns a view into a buffer where entropy can be drawn from.
  constexpr ktl::span<const uint8_t, 32> contents() const { return contents_; }

 private:
  // Matches |SHA256_DIGEST_LENGTH|. This is verified through static assertion in entropy_pool.cc.
  static constexpr size_t kContentSize = 32;

  ktl::array<uint8_t, kContentSize> contents_ = {};
};

}  // namespace crypto

#endif  // ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_ENTROPY_POOL_H_
