// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_GLUE_CRYPTO_BASE64_H_
#define PERIDOT_BIN_LEDGER_GLUE_CRYPTO_BASE64_H_

#include <string>

#include "lib/fxl/strings/string_view.h"

namespace glue {

// Encodes the input string in base64url.
std::string Base64UrlEncode(fxl::StringView input);

// Decodes the base64url input string. Returns true if successful and false
// otherwise. The output string is only modified if successful. The decoding can
// be done in-place.
bool Base64UrlDecode(fxl::StringView input, std::string* output);

}  // namespace glue

#endif  // PERIDOT_BIN_LEDGER_GLUE_CRYPTO_BASE64_H_
