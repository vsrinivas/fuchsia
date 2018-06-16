// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <crypto/secret.h>
#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/debug.h>
#include <openssl/mem.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace crypto {

// Public methods

Secret::Secret() : buf_(nullptr), len_(0) {}

Secret::~Secret() {
    Clear();
}

zx_status_t Secret::Allocate(size_t len, uint8_t** out) {
    ZX_DEBUG_ASSERT(len != 0 && out);

    Clear();
    fbl::AllocChecker ac;
    buf_.reset(new (&ac) uint8_t[len]);
    if (!ac.check()) {
        xprintf("failed to allocate %zu bytes\n", len);
        return ZX_ERR_NO_MEMORY;
    }
    memset(buf_.get(), 0, len);
    len_ = len;

    *out = buf_.get();
    return ZX_OK;
}

zx_status_t Secret::Generate(size_t len) {
    ZX_DEBUG_ASSERT(len != 0);

    uint8_t* tmp = nullptr;
    zx_status_t status = Allocate(len, &tmp);
    if (status != ZX_OK) {
        return status;
    }

    zx_cprng_draw(buf_.get(), len);
    return ZX_OK;
}

void Secret::Clear() {
    if (buf_) {
        mandatory_memset(buf_.get(), 0, len_);
    }
    buf_.reset();
    len_ = 0;
}

} // namespace crypto
