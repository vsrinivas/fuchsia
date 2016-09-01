// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLUE_CRYPTO_HASH_H_
#define GLUE_CRYPTO_HASH_H_

#include <string>

namespace glue {

std::string SHA256Hash(const void* input, size_t input_lenght);

}  // namespace glue

#endif  // GLUE_CRYPTO_HASH_H_
