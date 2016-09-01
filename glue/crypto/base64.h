// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLUE_CRYPTO_BASE64_H_
#define GLUE_CRYPTO_BASE64_H_

#include <string>

namespace glue {

// Encodes the input string in base64. The encoding can be done in-place.
void Base64Encode(const std::string& input, std::string* output);

// Decodes the base64 input string.  Returns true if successful and false
// otherwise. The output string is only modified if successful. The decoding can
// be done in-place.
bool Base64Decode(const std::string& input, std::string* output);

}  // namespace glue

#endif  // GLUE_CRYPTO_BASE64_H_
