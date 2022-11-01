// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "handoff-entropy.h"

#include <ctype.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstdint>

#include <explicit-memory/bytes.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/string_view.h>

#include <ktl/enforce.h>

void EntropyHandoff::AddEntropy(ktl::span<ktl::byte> payload) {
  if (payload.size() < crypto::kMinEntropyBytes) {
    fprintf(log_, "ZBI_TYPE_SECURE_ENTROPY too small: %zu < %zu\n", payload.size(),
            static_cast<size_t>(crypto::kMinEntropyBytes));
    return;
  }

  pool_.Add({reinterpret_cast<const uint8_t*>(payload.data()), payload.size()});
  mandatory_memset(payload.data(), 0, payload.size());
  has_valid_item_ = true;
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  // Verify that the payload contents have been zeroed.
  for (auto b : payload) {
    ZX_DEBUG_ASSERT(static_cast<char>(b) == 0);
  }
#endif
}

void EntropyHandoff::AddEntropy(BootOptions& options) {
  ktl::string_view cmdline_entropy{options.entropy_mixin};

  if (cmdline_entropy.empty()) {
    return;
  }

  for (auto c : cmdline_entropy) {
    if (isxdigit(c) == 0) {
      ZX_PANIC("'kernel.mixin-entropy' must be a valid hex string. Found %c in %.*s.", c,
               static_cast<int>(cmdline_entropy.length()), cmdline_entropy.data());
    }
  }

  size_t digest_size = pool_.AddFromDigest(ktl::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(cmdline_entropy.data()), cmdline_entropy.size()));
  mandatory_memset(options.entropy_mixin.hex.data(), 'x', options.entropy_mixin.hex.size());
  size_t added_entropy = ktl::min(cmdline_entropy.size() / 2, digest_size);
  if (added_entropy >= crypto::kMinEntropyBytes) {
    has_valid_item_ = true;
  }
}

ktl::optional<crypto::EntropyPool> EntropyHandoff::Take(const BootOptions& options) && {
  if (HasEnoughEntropy()) {
    return ktl::move(pool_);
  }

  if (options.cprng_seed_require_cmdline) {
    ZX_PANIC(
        "ZBI_TYPE_SECURE_ENTROPY zbi item or 'kernel.mixin-entropy' command line option did not "
        "provide enough entropy.");
  }

  return ktl::nullopt;
}
