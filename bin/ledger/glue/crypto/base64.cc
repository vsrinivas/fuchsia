// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/crypto/base64.h"

#include <openssl/base64.h>

#include "lib/ftl/logging.h"

namespace glue {

namespace {

const uint8_t* ToUnsigned(ftl::StringView str) {
  return reinterpret_cast<const uint8_t*>(str.data());
}

uint8_t* ToUnsigned(std::string& str) {
  return reinterpret_cast<uint8_t*>(&str[0]);
}

}  // namespace

void Base64Encode(ftl::StringView input, std::string* output) {
  std::string tmp_output;
  size_t output_length;
  FTL_CHECK(EVP_EncodedLength(&output_length, input.size()));
  // In C++11, std::string guarantees that tmp_output[tmp_output.size()] is
  // legal and points to a '\0' character. The last byte of EVP_EncodeBlock() is
  // a '\0' that will override tmp_output[tmp_output.size()].
  tmp_output.resize(output_length - 1);
  EVP_EncodeBlock(ToUnsigned(tmp_output), ToUnsigned(input), input.size());
  output->swap(tmp_output);
}

bool Base64Decode(ftl::StringView input, std::string* output) {
  std::string tmp_output;
  size_t output_maxlength;
  if (!EVP_DecodedLength(&output_maxlength, input.size()))
    return false;
  tmp_output.resize(output_maxlength);
  size_t output_length;
  bool result =
      EVP_DecodeBase64(ToUnsigned(tmp_output), &output_length, output_maxlength,
                       ToUnsigned(input), input.size());
  if (result) {
    tmp_output.resize(output_length);
    output->swap(tmp_output);
  }
  return result;
}

}  // namespace glue
