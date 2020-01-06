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

#include <openssl/sha.h>

namespace digest {

// The previously opaque crypto implementation context.
struct Digest::Context {
  Context() {}
  ~Context() {}
  SHA256_CTX impl;
};

Digest::Digest() : bytes_{0} {}

Digest::Digest(const uint8_t* other) : bytes_{0} { *this = other; }

Digest::Digest(Digest&& o) {
  ctx_ = std::move(o.ctx_);
  memcpy(bytes_, o.bytes_, sizeof(bytes_));
  memset(o.bytes_, 0, sizeof(o.bytes_));
}

Digest::~Digest() {}

Digest& Digest::operator=(Digest&& o) {
  memcpy(bytes_, o.bytes_, sizeof(bytes_));
  return *this;
}

Digest& Digest::operator=(const uint8_t* rhs) {
  memcpy(bytes_, rhs, sizeof(bytes_));
  return *this;
}

zx_status_t Digest::Init() {
  fbl::AllocChecker ac;
  ctx_.reset(new (&ac) Context());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  SHA256_Init(&ctx_->impl);
  return ZX_OK;
}

void Digest::Update(const void* buf, size_t len) {
  ZX_DEBUG_ASSERT(len <= INT_MAX);
  ZX_DEBUG_ASSERT(ctx_ != nullptr);
  SHA256_Update(&ctx_->impl, buf, len);
}

const uint8_t* Digest::Final() {
  ZX_DEBUG_ASSERT(ctx_ != nullptr);
  SHA256_Final(bytes_, &ctx_->impl);
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

zx_status_t Digest::CopyTo(uint8_t* out, size_t len) const {
  if (len < sizeof(bytes_)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memset(out, 0, len);
  memcpy(out, bytes_, sizeof(bytes_));
  return ZX_OK;
}

bool Digest::operator==(const Digest& rhs) const {
  return memcmp(bytes_, rhs.bytes_, sizeof(bytes_)) == 0;
}

bool Digest::operator!=(const Digest& rhs) const { return !(*this == rhs); }

bool Digest::operator==(const uint8_t* rhs) const {
  return rhs ? memcmp(bytes_, rhs, sizeof(bytes_)) == 0 : false;
}

bool Digest::operator!=(const uint8_t* rhs) const { return !(*this == rhs); }

}  // namespace digest
