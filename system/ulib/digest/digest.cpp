// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <digest/digest.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <magenta/assert.h>
#include <magenta/errors.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

namespace digest {

Digest::Digest(const Digest& other) {
    ref_count_ = 0;
    *this = other;
}

Digest::Digest(const uint8_t* other) {
    ref_count_ = 0;
    *this = other;
}

Digest::~Digest() {
    MX_DEBUG_ASSERT(ref_count_ == 0);
}

Digest& Digest::operator=(const Digest& rhs) {
    MX_DEBUG_ASSERT(ref_count_ == 0);
    if (this != &rhs) {
        memcpy(&ctx_, &rhs.ctx_, sizeof(ctx_));
        memcpy(bytes_, rhs.bytes_, kLength);
    }
    return *this;
}

Digest& Digest::operator=(const uint8_t* rhs) {
    MX_DEBUG_ASSERT(ref_count_ == 0);
    memcpy(bytes_, rhs, kLength);
    return *this;
}

void Digest::Init() {
    MX_DEBUG_ASSERT(ref_count_ == 0);
#ifdef USE_LIBCRYPTO
    SHA256_Init(&ctx_);
#else
    clSHA256_init(&ctx_);
#endif // USE_LIBCRYPTO
}

void Digest::Update(const void* buf, size_t len) {
    MX_DEBUG_ASSERT(ref_count_ == 0);
    MX_DEBUG_ASSERT(len <= INT_MAX);
#ifdef USE_LIBCRYPTO
    SHA256_Update(&ctx_, buf, len);
#else
    clHASH_update(&ctx_, buf, static_cast<int>(len));
#endif // USE_LIBCRYPTO
}

const uint8_t* Digest::Final() {
    MX_DEBUG_ASSERT(ref_count_ == 0);
#ifdef USE_LIBCRYPTO
    SHA256_Final(bytes_, &ctx_);
#else
    memcpy(bytes_, clHASH_final(&ctx_), kLength);
#endif // USE_LIBCRYPTO
    return bytes_;
}

const uint8_t* Digest::Hash(const void* buf, size_t len) {
    Init();
    Update(buf, len);
    return Final();
}

mx_status_t Digest::Parse(const char* hex, size_t len) {
    MX_DEBUG_ASSERT(ref_count_ == 0);
    if (len < sizeof(bytes_) * 2) {
        return MX_ERR_INVALID_ARGS;
    }
    uint8_t c = 0;
    size_t i = 0;
    for (size_t j = 0; j < sizeof(bytes_) * 2; ++j) {
        c = static_cast<uint8_t>(toupper(hex[j]) & 0xFF);
        if (!isxdigit(c)) {
            return MX_ERR_INVALID_ARGS;
        }
        c = static_cast<uint8_t>(c < 'A' ? c - '0' : c - '7'); // '7' = 'A' - 10
        if (j % 2 == 0) {
            bytes_[i] = static_cast<uint8_t>(c << 4);
        } else {
            bytes_[i++] |= c;
        }
    }
    return MX_OK;
}

mx_status_t Digest::ToString(char* out, size_t len) const {
    if (len < sizeof(bytes_) * 2 + 1) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }
    memset(out, 0, len);
    char* p = out;
    for (size_t i = 0; i < sizeof(bytes_); ++i) {
        sprintf(p, "%02x", bytes_[i]);
        p += 2;
    }
    return MX_OK;
}

mx_status_t Digest::CopyTo(uint8_t* out, size_t len) const {
    if (len < sizeof(bytes_)) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }
    memset(out, 0, len);
    memcpy(out, bytes_, sizeof(bytes_));
    return MX_OK;
}

const uint8_t* Digest::AcquireBytes() const {
    MX_DEBUG_ASSERT(ref_count_ < SIZE_MAX);
    ++ref_count_;
    return bytes_;
}

void Digest::ReleaseBytes() const {
    MX_DEBUG_ASSERT(ref_count_ > 0);
    --ref_count_;
}

bool Digest::operator==(const Digest& rhs) const {
    return memcmp(bytes_, rhs.bytes_, kLength) == 0;
}

bool Digest::operator!=(const Digest& rhs) const {
    return !(*this == rhs);
}

bool Digest::operator==(const uint8_t* rhs) const {
    return rhs ? memcmp(bytes_, rhs, kLength) == 0 : false;
}

bool Digest::operator!=(const uint8_t* rhs) const {
    return !(*this == rhs);
}

} // namespace digest

using digest::Digest;

// C-style wrapper functions
struct digest_t {
    Digest obj;
};

mx_status_t digest_init(digest_t** out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<digest_t> uptr(new (&ac) digest_t);
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    uptr->obj.Init();
    *out = uptr.release();
    return MX_OK;
}

void digest_update(digest_t* digest, const void* buf, size_t len) {
    digest->obj.Update(buf, len);
}

mx_status_t digest_final(digest_t* digest, void* out,
                                size_t out_len) {
    fbl::unique_ptr<digest_t> uptr(digest);
    uptr->obj.Final();
    return uptr->obj.CopyTo(static_cast<uint8_t*>(out), out_len);
}

mx_status_t digest_hash(const void* buf, size_t len, void* out,
                               size_t out_len) {
    Digest digest;
    digest.Hash(buf, len);
    return digest.CopyTo(static_cast<uint8_t*>(out), out_len);
}
