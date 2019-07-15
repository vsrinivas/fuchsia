// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_GLOBAL_PRNG_H_
#define ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_GLOBAL_PRNG_H_

#include <lib/crypto/prng.h>

namespace crypto {

namespace GlobalPRNG {

// Returns a pointer to the global PRNG singleton.  The pointer is
// guaranteed to be non-null.
PRNG* GetInstance();

}  // namespace GlobalPRNG

}  // namespace crypto

#endif  // ZIRCON_KERNEL_LIB_CRYPTO_INCLUDE_LIB_CRYPTO_GLOBAL_PRNG_H_
