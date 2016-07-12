// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <debug.h>
#include <stddef.h>
#include <stdint.h>

#include <lib/crypto/cryptolib.h>

namespace crypto {

// This class implements a cryptographically secure 256 bits hash function.
// It can be used in two ways:
//
// 1.
// Hash256 hash1("abc", 3);
// hash1.digest() returns the hash of "abc"
//
// 2.
// Hash256 hash2;
// hash2.Update("a", 1);
// hash2.Update("bc", 2);
// hash2.Final();
// hash2.digest() returns the hash of "abc".
class Hash256 {
public:
    static const size_t kHashSize = 256 / 8;
    Hash256();
    Hash256(const void* data, int len);
    ~Hash256();

    void Update(const void* data, int len);
    void Final();
    const uint8_t* digest() const { return digest_; }

private:
    Hash256(const Hash256&) = delete;
    Hash256& operator=(const Hash256&) = delete;

    clSHA256_CTX ctx_;
    const uint8_t* digest_;
#if LK_DEBUGLEVEL > 0
    bool finalized_;
#endif
};

} // namespace crypto
