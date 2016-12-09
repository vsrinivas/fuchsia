// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/firebase/encoding.h"

#include "apps/ledger/src/glue/crypto/base64.h"
#include "lib/ftl/strings/utf_codecs.h"

namespace firebase {

namespace {

// Returns true iff the given value can be put in Firebase without encoding.
// Firebase requires the values to be valid UTF-8 JSON strings. JSON disallows
// control characters in strings. We disallow backslash and double quote to
// avoid reasoning about escaping. Note: this is a stop-gap solution, see
// LE-118.
bool CanValueBeVerbatim(const std::string& bytes) {
  // Once encryption is in place this won't be useful. Until then, storing valid
  // utf8 strings verbatim simplifies debugging.
  if (!ftl::IsStringUTF8(bytes)) {
    return false;
  }

  for (const char& byte : bytes) {
    if ((0 <= byte && byte <= 31) || byte == 127 || byte == '\"' ||
        byte == '\\') {
      return false;
    }
  }

  return true;
}

// Characters that are not allowed to appear in a Firebase key (but may appear
// in a value). See
// https://firebase.google.com/docs/database/rest/structure-data.
const char kIllegalKeyChars[] = ".$#[]/+";

// Returns true if the given value can be used as a Firebase key without
// encoding.
bool CanKeyBeVerbatim(const std::string& bytes) {
  if (!CanValueBeVerbatim(bytes)) {
    return false;
  }

  if (bytes.find_first_of(std::string(kIllegalKeyChars)) != std::string::npos) {
    return false;
  }

  return true;
}

// Encodes the given bytes for storage in Firebase. We use the same encoding
// function for both values and keys for simplicity, yielding values that can be
// always safely used as either. Note: this is a stop-gap solution, see LE-118.
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

std::string EncodeKey(convert::ExtendedStringView bytes) {
  std::string s(bytes.data(), bytes.size());
  return Encode(s, CanKeyBeVerbatim(s));
}

std::string EncodeValue(convert::ExtendedStringView bytes) {
  std::string s(bytes.data(), bytes.size());
  return Encode(s, CanValueBeVerbatim(s));
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
