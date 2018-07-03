// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_BASE64URL_BASE64URL_H_
#define PERIDOT_LIB_BASE64URL_BASE64URL_H_

#include <string>

#include <lib/fxl/strings/string_view.h>

namespace base64url {

// Encodes the input string in base64url.
std::string Base64UrlEncode(fxl::StringView input);

// Decodes the base64url input string. Returns true if successful and false
// otherwise. The output string is only modified if successful. The decoding can
// be done in-place.
bool Base64UrlDecode(fxl::StringView input, std::string* output);

}  // namespace base64url

#endif  // PERIDOT_LIB_BASE64URL_BASE64URL_H_
