// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_ENCRYPT_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_ENCRYPT_H_

#include <lib/fxl/strings/string_view.h>

namespace encryption {

// Encrypt the given |data| with the given |key| using AES128-GCM-SIV. The key
// size must be 128 bits.
bool AES128GCMSIVEncrypt(fxl::StringView key, fxl::StringView data,
                         std::string* output);

// Descript the given |encrypteddata| with the given |key| using AES128-GCM-SIV.
// The key size must be 128 bits.
bool AES128GCMSIVDecrypt(fxl::StringView key, fxl::StringView encrypted_data,
                         std::string* output);

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_ENCRYPT_H_
