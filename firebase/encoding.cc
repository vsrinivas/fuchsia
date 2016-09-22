// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/firebase/encoding.h"

#include "apps/ledger/glue/crypto/base64.h"
#include "lib/ftl/strings/utf_codecs.h"

namespace firebase {

namespace {

// Characters that are not allowed to appear in a Firebase key (but may appear
// in the leaf). See
// https://www.firebase.com/docs/rest/guide/understanding-data.html#section-limitations
const char kIllegalKeyChars[] =
    ".$#[]/+"
    "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
    "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
    "\x7F";
const size_t kIllegalKeyCharsCount = sizeof(kIllegalKeyChars) - 1;

// Characters not allowed neither as keys nor values.
const char kIllegalChars[] = "\x00\"\\";
const size_t kIllegalCharsCount = sizeof(kIllegalChars) - 1;

bool IsValidValue(const std::string& s) {
  if (!ftl::IsStringUTF8(s)) {
    return false;
  }

  if (s.find_first_of(std::string(kIllegalChars, kIllegalCharsCount)) !=
      std::string::npos) {
    return false;
  }

  return true;
}

bool IsValidKey(const std::string& s) {
  if (!IsValidValue(s)) {
    return false;
  }

  // This can return false negatives when one of the forbidden bytes appears as
  // part of a multibyte character. For our purposes this is acceptable, as we
  // just fall back to base64.
  return s.find_first_of(std::string(
             kIllegalKeyChars, kIllegalKeyCharsCount)) == std::string::npos;
}

std::string Encode(const std::string& s, bool verbatim) {
  if (verbatim) {
    return s + "V";
  }

  std::string encoded;
  glue::Base64Encode(s, &encoded);
  std::replace(encoded.begin(), encoded.end(), '/', '-');
  std::replace(encoded.begin(), encoded.end(), '+', '_');
  return encoded + "B";
}

}  // namespace

std::string EncodeKey(const convert::BytesReference& bytes) {
  std::string s(bytes.data(), bytes.size());
  return Encode(s, IsValidKey(s));
}

std::string EncodeValue(const convert::BytesReference& bytes) {
  std::string s(bytes.data(), bytes.size());
  return Encode(s, IsValidValue(s));
}

bool Decode(const std::string& input, std::string* output) {
  if (input.empty()) {
    return false;
  }

  if (input.back() == 'V') {
    *output = std::string(input.data(), input.size() - 1);
    return true;
  }

  if (input.back() == 'B') {
    std::string encoded(input.data(), input.size() - 1);
    std::replace(encoded.begin(), encoded.end(), '_', '+');
    std::replace(encoded.begin(), encoded.end(), '-', '/');
    return glue::Base64Decode(encoded, output);
  }

  return false;
}

}  // namespace firebase
