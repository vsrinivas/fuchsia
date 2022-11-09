// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CRYPT_POLICY_H_
#define SRC_STORAGE_FSHOST_CRYPT_POLICY_H_

#include "src/security/lib/zxcrypt/client.h"

namespace fshost {

// Re-export some symbols from zxcrypt client.  We use the same policy engine for determining Fxfs'
// key policy.
using ::zxcrypt::ComputeEffectiveCreatePolicy;
using ::zxcrypt::ComputeEffectiveUnsealPolicy;
using ::zxcrypt::KeySource;
using ::zxcrypt::KeySourcePolicy;
using ::zxcrypt::SelectKeySourcePolicy;

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_CRYPT_POLICY_H_
