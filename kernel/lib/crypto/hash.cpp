// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/hash.h>

#include <assert.h>
#include <debug.h>
#include <lib/crypto/cryptolib.h>

namespace crypto {

Hash256::Hash256()
    : digest_(0) {
#if LK_DEBUGLEVEL > 0
    finalized_ = false;
#endif
    clSHA256_init(&ctx_);
}

Hash256::Hash256(const void* data, int len)
    : Hash256() {
    Hash256::Update(data, len);
    Hash256::Final();
}

Hash256::~Hash256() {}

void Hash256::Update(const void* data, int len) {
    DEBUG_ASSERT(!finalized_);
    clHASH_update(&ctx_, data, len);
}

void Hash256::Final() {
#if LK_DEBUGLEVEL > 0
    DEBUG_ASSERT(!finalized_);
    finalized_ = true;
#endif

    digest_ = clHASH_final(&ctx_);
}

} // namespace crypto
