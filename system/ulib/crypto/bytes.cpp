// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <crypto/bytes.h>
#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <openssl/mem.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define MXDEBUG 0

namespace crypto {

// Public methods

Bytes::Bytes() : buf_(nullptr), len_(0) {}

Bytes::~Bytes() {
    Reset();
}

void Bytes::Adopt(fbl::unique_ptr<uint8_t[]> buf, size_t len) {
    Reset();
    buf_ = fbl::move(buf);
    len_ = len;
}

zx_status_t Bytes::Init(size_t size, uint8_t fill) {
    Reset();
    return Resize(size, fill);
}

zx_status_t Bytes::Resize(size_t size, uint8_t fill) {
    // Early exit if truncating to zero.
    if (size == 0) {
        Reset();
        return ZX_OK;
    }

    // Allocate new memory
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> tmp(new (&ac) uint8_t[size]);
    if (!ac.check()) {
        xprintf("%s: allocation failed: %zu bytes\n", __PRETTY_FUNCTION__, size);
        return ZX_ERR_NO_MEMORY;
    }

    // Fill it with old data and pad as needed
    if (len_ == 0) {
        memset(tmp.get(), fill, size);
    } else if (len_ < size) {
        memcpy(tmp.get(), buf_.get(), len_);
        memset(tmp.get() + len_, fill, size - len_);
    } else {
        memcpy(tmp.get(), buf_.get(), size);
    }

    Reset();
    len_ = size;
    buf_ = fbl::move(tmp);
    return ZX_OK;
}

zx_status_t Bytes::Copy(const void* buf, size_t len, zx_off_t off) {
    zx_status_t rc;

    if (len == 0) {
        return ZX_OK;
    }

    if (!buf) {
        xprintf("%s: null buffer\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;
    }

    size_t size = off + len;
    ZX_ASSERT(size >= len); // overflow
    if (len_ < size && (rc = Resize(size)) != ZX_OK) {
        return rc;
    }
    memcpy(buf_.get() + off, buf, len);

    return ZX_OK;
}

zx_status_t Bytes::Randomize(size_t size) {
    zx_status_t rc;

    if ((rc = Resize(size)) != ZX_OK) {
        return rc;
    }
    uint8_t* p = buf_.get();
    size_t actual;
    while (size != 0) {
        size_t n = fbl::min(size, static_cast<size_t>(ZX_CPRNG_DRAW_MAX_LEN));
        if ((rc = zx_cprng_draw(p, n, &actual)) != ZX_OK) {
            xprintf("%s: zx_cprng_draw(%p, %zu, %p) failed: %s", __PRETTY_FUNCTION__, p, n, &actual,
                    zx_status_get_string(rc));
            return rc;
        }
        p += actual;
        size -= actual;
    }

    return ZX_OK;
}

fbl::unique_ptr<uint8_t[]> Bytes::Release(size_t* len) {
    if (len) {
        *len = len_;
    }
    len_ = 0;
    return fbl::move(buf_);
}

void Bytes::Reset() {
    if (buf_) {
        mandatory_memset(buf_.get(), 0, len_);
    }
    buf_.reset();
    len_ = 0;
}

const uint8_t& Bytes::operator[](zx_off_t off) const {
    ZX_ASSERT(off < len_);
    return buf_[off];
}

uint8_t& Bytes::operator[](zx_off_t off) {
    ZX_ASSERT(off < len_);
    return buf_[off];
}

bool Bytes::operator==(const Bytes& other) const {
    if (len_ != other.len_) {
        return false;
    } else if (len_ == 0) {
        return true;
    } else {
        ZX_DEBUG_ASSERT(buf_.get());
        ZX_DEBUG_ASSERT(other.buf_.get());
        return CRYPTO_memcmp(buf_.get(), other.buf_.get(), len_) == 0;
    }
}

} // namespace crypto
