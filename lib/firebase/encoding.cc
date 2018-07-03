// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/encoding.h"

#include <lib/fxl/strings/utf_codecs.h>

#include "peridot/lib/base64url/base64url.h"

namespace firebase {

namespace {

// Returns true iff the given value can be put in Firebase without encoding.
// Firebase requires the values to be valid UTF-8 JSON strings. JSON disallows
// control characters in strings. We disallow backslash and double quote to
// avoid reasoning about escaping. Note: this is a stop-gap solution, see
// LE-118.
bool CanValueBeVerbatim(fxl::StringView bytes) {
  // Once encryption is in place this won't be useful. Until then, storing valid
  // utf8 strings verbatim simplifies debugging.
  if (!fxl::IsStringUTF8(bytes)) {
    return false;
  }

  for (const char& byte : bytes) {
    if (static_cast<unsigned char>(byte) <= 31 || byte == 127 || byte == '\"' ||
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

// Encodes the given bytes for storage in Firebase. We use the same encoding
// function for both values and keys for simplicity, yielding values that can be
// always safely used as either. Note: this is a stop-gap solution, see LE-118.
std::string Encode(fxl::StringView s, bool verbatim) {
  if (verbatim) {
    return s.ToString() + "V";
  }

  std::string encoded;
  return base64url::Base64UrlEncode(s) + "B";
}

}  // namespace

// Returns true if the given value can be used as a Firebase key without
// encoding.
bool CanKeyBeVerbatim(fxl::StringView bytes) {
  if (!CanValueBeVerbatim(bytes)) {
    return false;
  }

  if (bytes.find_first_of(std::string(kIllegalKeyChars)) != std::string::npos) {
    return false;
  }

  return true;
}

std::string EncodeKey(convert::ExtendedStringView bytes) {
  return Encode(bytes, CanKeyBeVerbatim(bytes));
}

std::string EncodeValue(convert::ExtendedStringView bytes) {
  return Encode(bytes, CanValueBeVerbatim(bytes));
}

bool Decode(convert::ExtendedStringView input, std::string* output) {
  if (input.empty()) {
    return false;
  }

  fxl::StringView data = input.substr(0, input.size() - 1);

  if (input.back() == 'V') {
    *output = data.ToString();
    return true;
  }

  if (input.back() == 'B') {
    return base64url::Base64UrlDecode(data, output);
  }

  return false;
}

}  // namespace firebase
