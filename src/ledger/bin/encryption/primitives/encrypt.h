// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_ENCRYPT_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_ENCRYPT_H_

#include <src/lib/fxl/strings/string_view.h>

#include "peridot/lib/rng/random.h"

namespace encryption {

// Encrypt the given |data| with the given |key| using AES128-GCM-SIV. The key
// size must be 128 bits.
bool AES128GCMSIVEncrypt(rng::Random* random, fxl::StringView key,
                         fxl::StringView data, std::string* output);

// Descript the given |encrypteddata| with the given |key| using AES128-GCM-SIV.
// The key size must be 128 bits.
bool AES128GCMSIVDecrypt(fxl::StringView key, fxl::StringView encrypted_data,
                         std::string* output);

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_ENCRYPT_H_
