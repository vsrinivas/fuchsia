// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/digest.h>
#include <fbl/macros.h>
#include <zircon/types.h>

// |crypto::hmac| is a block-sized hash-bashed message authentication code.
namespace crypto {

class HMAC final {
public:
    enum Flags : uint16_t {
        ALLOW_WEAK_KEY = 0x0001,
        ALLOW_TRUNCATION = 0x0002,
    };

    HMAC();
    ~HMAC();

    // Convenience method that calls |Init|, |Update|, and |Final| in one shot to create a keyed
    // digest that it saves in |out|. Callers must omit |flags| unless the security implications
    // are clearly understood.
    static zx_status_t Create(digest::Algorithm digest, const Bytes& key, const void* in,
                              size_t in_len, Bytes* out, uint16_t flags = 0);

    // Convenience method that checks if the given |digest| matches the one that |Create| would
    // generate using |digest|, |key|, |in|, and |in_len|.  On failure, it returns
    // |ZX_ERR_IO_DATA_INTEGRITY|. Callers must omit |flags| unless the security implications are
    // clearly understood.
    static zx_status_t Verify(digest::Algorithm digest, const Bytes& key, const void* in,
                              size_t in_len, const Bytes& hmac, uint16_t flags = 0);

    // Initializes the HMAC algorithm indicated by |digest| with the given |key|.  A call to |Init|
    // must precede any calls to |Update| or |Final|. Callers must omit |flags| unless the security
    // implications are clearly understood.
    zx_status_t Init(digest::Algorithm digest, const Bytes& key, uint16_t flags = 0);

    // Updates the HMAC with |in_len| bytes of additional data from |in|. This can only be called
    // between calls to |Init| and |Final|.
    zx_status_t Update(const void* in, size_t in_len);

    // Returns the keyed digest in |out|.  |Init| must be called again before calling |Update|
    // again.
    zx_status_t Final(Bytes* out);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(HMAC);

    // Opaque crypto implementation context.
    struct Context;

    // Opaque pointer to the crypto implementation context.
    fbl::unique_ptr<Context> ctx_;
};

} // namespace crypto
