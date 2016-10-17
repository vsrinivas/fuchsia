// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLUE_CRYPTO_HASH_H_
#define GLUE_CRYPTO_HASH_H_

#include <memory>
#include <string>

#include "lib/ftl/macros.h"

namespace glue {

class SHA256StreamingHash {
 public:
  SHA256StreamingHash();
  ~SHA256StreamingHash();
  void Update(const void* input, size_t len);
  void Finish(std::string* output);

 private:
  struct Context;
  std::unique_ptr<Context> context_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SHA256StreamingHash);
};

std::string SHA256Hash(const void* input, size_t input_lenght);

}  // namespace glue

#endif  // GLUE_CRYPTO_HASH_H_
