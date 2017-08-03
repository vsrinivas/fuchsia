// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/crypto/prng.h>

namespace crypto {

namespace GlobalPRNG {

// Returns a pointer to the global PRNG singleton.  The pointer is
// guaranteed to be non-null.
PRNG* GetInstance();

} //namespace GlobalPRNG

} // namespace crypto
