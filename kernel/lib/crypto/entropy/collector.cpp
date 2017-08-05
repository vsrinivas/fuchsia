// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/collector.h>

#include <assert.h>

namespace crypto {

namespace entropy {

Collector::Collector(const char* name, size_t entropy_per_1000_bytes)
    : name_(name, strnlen(name, MX_MAX_NAME_LEN)),
      entropy_per_1000_bytes_(entropy_per_1000_bytes) {
    DEBUG_ASSERT(entropy_per_1000_bytes_ > 0);
    DEBUG_ASSERT(entropy_per_1000_bytes_ <= 8000);
}

Collector::~Collector() {
}

size_t Collector::BytesNeeded(size_t bits) const {
    // This avoids overflow, and (probably more likely) programming errors.
    DEBUG_ASSERT(bits <= (1024u * 1024u));
    // Round up, to ensure that the result always contains at least the
    // requested amount of entropy.
    return (1000u * bits + entropy_per_1000_bytes_ - 1u)
            / entropy_per_1000_bytes_;
}

} // namespace entropy

} // namespace crypto
