// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FCRYPTO_ERROR_H_
#define SRC_SECURITY_LIB_FCRYPTO_ERROR_H_

#include <zircon/types.h>

namespace crypto {

// Prints the crypto errors.  Use when a call to the crypto implementation indicates failure.
void xprintf_crypto_errors(zx_status_t* out);

}  // namespace crypto

#endif  // SRC_SECURITY_LIB_FCRYPTO_ERROR_H_
