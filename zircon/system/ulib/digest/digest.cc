// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>

#include <digest/digest.h>
#include <fbl/alloc_checker.h>
#include <fbl/string.h>

// See note in //zircon/third_party/ulib/uboringssl/rules.mk
#define BORINGSSL_NO_CXX
#include <utility>

#include <openssl/mem.h>
#include <openssl/sha.h>

namespace digest {

// The previously opaque crypto implementation context.
struct Digest::Context {
  SHA256_CTX impl;
};

Digest::Digest() : bytes_{0} {}

Digest::Digest(const uint8_t (&bytes)[sizeof(bytes_)]) { *this = bytes; }

Digest::Digest(Digest&& other) { *this = std::move(other); }

Digest::~Digest() {}

Digest& Digest::operator=(const uint8_t (&bytes)[sizeof(bytes_)]) {
  ctx_.reset();
  memcpy(bytes_, bytes, sizeof(bytes_));
  return *this;
}

Digest& Digest::operator=(Digest&& other) {
  ctx_ = std::move(other.ctx_);
  memcpy(bytes_, other.bytes_, sizeof(bytes_));
  memset(other.bytes_, 0, sizeof(other.bytes_));
  return *this;
}

void Digest::Init() {
  ctx_.reset(new Context());
  SHA256_Init(&ctx_->impl);
}

void Digest::Update(const void* buf, size_t len) {
  ZX_DEBUG_ASSERT(ctx_);
  ZX_DEBUG_ASSERT(len <= INT_MAX);
  SHA256_Update(&ctx_->impl, buf, len);
}

const uint8_t* Digest::Final() {
  ZX_DEBUG_ASSERT(ctx_);
  SHA256_Final(bytes_, &ctx_->impl);
  ctx_.reset();
  return bytes_;
}

const uint8_t* Digest::Hash(const void* buf, size_t len) {
  Init();
  Update(buf, len);
  return Final();
}

zx_status_t Digest::Parse(const char* hex, size_t len) {
  if (len != sizeof(bytes_) * 2) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t i = 0;
  for (size_t j = 0; j < sizeof(bytes_) * 2; ++j) {
    int c = toupper(hex[j]);
    if (!isxdigit(c)) {
      return ZX_ERR_INVALID_ARGS;
    }
    c = c < 'A' ? c - '0' : c - '7';  // '7' = 'A' - 10
    if (j % 2 == 0) {
      bytes_[i] = static_cast<uint8_t>(c << 4);
    } else {
      bytes_[i++] |= static_cast<uint8_t>(c);
    }
  }
  return ZX_OK;
}

fbl::String Digest::ToString() const {
  char hex[kSha256HexLength + 1];
  char* p = hex;
  for (size_t i = 0; i < sizeof(bytes_); ++i) {
    sprintf(p, "%02x", bytes_[i]);
    p += 2;
  }
  return fbl::String(hex);
}

void Digest::CopyTo(uint8_t* out, size_t len) const {
  ZX_DEBUG_ASSERT(len >= sizeof(bytes_));
  CopyTruncatedTo(out, len);
}

void Digest::CopyTruncatedTo(uint8_t* out, size_t len) const {
  if (len == 0) {
    return;
  } else if (len <= sizeof(bytes_)) {
    memcpy(out, bytes_, len);
  } else {
    memcpy(out, bytes_, sizeof(bytes_));
    out += sizeof(bytes_);
    len -= sizeof(bytes_);
    memset(out, 0, len);
  }
}

bool Digest::Equals(const uint8_t* rhs, size_t len) const {
  return rhs && len == sizeof(bytes_) && CRYPTO_memcmp(bytes_, rhs, sizeof(bytes_)) == 0;
}

}  // namespace digest
