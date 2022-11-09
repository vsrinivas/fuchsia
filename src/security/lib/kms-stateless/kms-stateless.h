/*
 * Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_SECURITY_LIB_KMS_STATELESS_KMS_STATELESS_H_
#define SRC_SECURITY_LIB_KMS_STATELESS_KMS_STATELESS_H_

#include <lib/fit/function.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

namespace kms_stateless {
const size_t kExpectedKeyInfoSize = 32;

// The callback called when a hardware key is successfully derived. Arguments to the callback
// is a unique_ptr of the key buffer and the key size.
using GetHardwareDerivedKeyCallback =
    fit::function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)>;

// Get a hardware derived key using the device /dev/class/tee/000 .
// This is useful in early boot when other services may not be up.
zx_status_t GetHardwareDerivedKey(GetHardwareDerivedKeyCallback callback,
                                  uint8_t key_info[kExpectedKeyInfoSize]);

// Get a hardware derived key using the service fuchsia.tee.Application .
// This should be used from components.
zx_status_t GetHardwareDerivedKeyFromService(GetHardwareDerivedKeyCallback callback,
                                             uint8_t key_info[kExpectedKeyInfoSize]);

// Rotate an existing hardware derived key identified by `key_info` using the service
// fuchsia.tee.Application.
//
// This should be used from components.
zx_status_t RotateHardwareDerivedKeyFromService(uint8_t key_info[kExpectedKeyInfoSize]);

}  // namespace kms_stateless

#endif  // SRC_SECURITY_LIB_KMS_STATELESS_KMS_STATELESS_H_
