// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_ENCODING_H_
#define PERIDOT_LIB_FIREBASE_ENCODING_H_

#include <string>

#include "peridot/lib/convert/convert.h"

namespace firebase {

// Returns true iff the given value can be put in Firebase as a key name without
// encoding.
bool CanKeyBeVerbatim(fxl::StringView bytes);

// Warning: this is a naive solution which needs multiple passes and copies of
// the data each time. To be optimized if we are going to use this in the target
// implementation - see LE-118.

// These methods encode the given bytes as a valid Firebase key / value.
//
// Strings that are already valid Firebase keys / values are encoded as:
// "<original string>V" ("V" standing for "verbatim". This saves bytes (compared
// to base64) and allows to make sense of the data upon manual inspection.
//
// Strings that are not valid Firebase keys / values are encoded as base64 with
// slashes replaced with backslashes and "B" added at the end.
std::string EncodeKey(convert::ExtendedStringView bytes);
std::string EncodeValue(convert::ExtendedStringView bytes);

// Returns true iff the key or value was correctly decoded and stored in |out|.
// We don't need separate methods for keys and values, as the decoding algorithm
// is identical.
bool Decode(convert::ExtendedStringView input, std::string* output);

}  // namespace firebase

#endif  // PERIDOT_LIB_FIREBASE_ENCODING_H_
