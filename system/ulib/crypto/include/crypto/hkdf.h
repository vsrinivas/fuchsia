// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/digest.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

// |crypto::HKDF| is a key derivation function.  It can turn variable-length and/or weak input key
// material into cryptographically strong output key material.  However, this class does NOT do any
// key stretching.  It is the caller's responsibility to protect against brute forcing weak input
// key material, e.g. by requiring strong input key material or by rate-limiting the use of both
// |Init| and |Derive|.
namespace crypto {

class HKDF final {
public:
    enum Flags : uint16_t {
        ALLOW_WEAK_KEY = 0x0001,
    };

    HKDF();
    ~HKDF();

    // Initializes the HKDF algorithms indicated by |digest| with the input key material in |ikm|
    // and the given |salt|.  Callers must omit |flags| unless the security implications are clearly
    // understood.
    zx_status_t Init(digest::Algorithm digest, const Bytes& ikm, const Bytes& salt,
                     uint16_t flags = 0);

    // Fill |out_key| with output key material.  The key material will depend on the |ikm| and
    // |salt| given in |Init|, as well as the |label| provided here.  |out_key| will be the same if
    // and only if all of those parameters are unchanged.
    zx_status_t Derive(const char* label, Bytes* out_key);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(HKDF);

    // Digest algorithm used by this HKDF.
    digest::Algorithm digest_;
    // The pseudo-random key used to derive other keys
    Bytes prk_;
};

} // namespace crypto
