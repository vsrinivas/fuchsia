// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_BASE64URL_BASE64URL_H_
#define SRC_MODULAR_LIB_BASE64URL_BASE64URL_H_

#include <string>
#include <string_view>

namespace base64url {

// Encodes the input string in base64url.
std::string Base64UrlEncode(std::string_view input);

// Decodes the base64url input string. Returns true if successful and false
// otherwise. The output string is only modified if successful. The decoding can
// be done in-place.
bool Base64UrlDecode(std::string_view input, std::string* output);

}  // namespace base64url

#endif  // SRC_MODULAR_LIB_BASE64URL_BASE64URL_H_
