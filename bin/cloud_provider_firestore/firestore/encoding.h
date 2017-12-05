// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_ENCODING_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_ENCODING_H_

#include <string>

#include "lib/fxl/strings/string_view.h"

namespace cloud_provider_firestore {

// Encodes the data so that it can be used as a Firestore key.
//
// The resulting encoding is base64url with a single '+' character appended at
// the end. This is because Firestore disallows keys matching the regular
// expression `__.*__` which would otherwise be possible to produce.
//
// See https://cloud.google.com/firestore/quotas#limits.
std::string EncodeKey(fxl::StringView input);

// Decodes a Firestore key encoded using |EncodeKey|.
bool DecodeKey(fxl::StringView input, std::string* output);

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_ENCODING_H_
