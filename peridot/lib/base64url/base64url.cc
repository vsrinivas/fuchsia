// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/base64url/base64url.h"

#include <lib/fxl/logging.h>

#include "peridot/third_party/modp_b64/modp_b64.h"

namespace base64url {

std::string Base64UrlEncode(fxl::StringView input) {
  std::string output;
  size_t output_length = modp_b64_encode_strlen(input.size());
  // In C++11, std::string guarantees that output[output.size()] is
  // legal and points to a '\0' character. The last byte of modp_b64_encode() is
  // a '\0' that will override output[output.size()].
  output.resize(output_length);
  size_t written = modp_b64_encode(&output[0], input.data(), input.size());
  FXL_DCHECK(output_length == written);
  return output;
}

bool Base64UrlDecode(fxl::StringView input, std::string* output) {
  std::string tmp_output;
  size_t output_maxlength = modp_b64_decode_len(input.size());
  tmp_output.resize(output_maxlength);
  size_t output_length =
      modp_b64_decode(&tmp_output[0], input.data(), input.size());
  if (output_length == MODP_B64_ERROR) {
    return false;
  }
  tmp_output.resize(output_length);
  output->swap(tmp_output);
  return true;
}

}  // namespace base64url
