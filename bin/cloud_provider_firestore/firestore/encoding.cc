// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"

#include "peridot/lib/base64url/base64url.h"

namespace cloud_provider_firestore {

std::string EncodeKey(fxl::StringView input) {
  std::string encoded = base64url::Base64UrlEncode(input);
  encoded.append(1u, '+');
  return encoded;
}

bool DecodeKey(fxl::StringView input, std::string* output) {
  if (input.empty() || input.back() != '+') {
    return false;
  }

  input.remove_suffix(1u);
  return base64url::Base64UrlDecode(input, output);
}

}  // namespace cloud_provider_firestore
