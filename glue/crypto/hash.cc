// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/crypto/hash.h"

#include <openssl/sha.h>

#include "lib/ftl/logging.h"

namespace glue {

struct SHA256StreamingHash::Context {
  SHA256_CTX sha256;
};

SHA256StreamingHash::~SHA256StreamingHash() {}

SHA256StreamingHash::SHA256StreamingHash() : context_(new Context()) {
  SHA256_Init(&context_->sha256);
}

void SHA256StreamingHash::Update(const void* input, size_t len) {
  SHA256_Update(&context_->sha256, input, len);
}

void SHA256StreamingHash::Finish(std::string* output) {
  output->resize(SHA256_DIGEST_LENGTH);
  SHA256_Final(reinterpret_cast<uint8_t*>(&(*output)[0]), &context_->sha256);
}

std::string SHA256Hash(const void* input, size_t input_lenght) {
  std::string result;
  result.resize(SHA256_DIGEST_LENGTH);
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, input, input_lenght);
  SHA256_Final(reinterpret_cast<uint8_t*>(&result[0]), &sha256);
  return result;
}

}  // namespace glue
