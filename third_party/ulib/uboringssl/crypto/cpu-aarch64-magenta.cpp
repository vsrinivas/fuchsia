// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/cpu.h>

#if defined(OPENSSL_AARCH64) && !defined(OPENSSL_STATIC_ARMCAP)

#include <openssl/arm_arch.h>

#include "internal.h"

void OPENSSL_cpuid_setup(void) {
    // TODO(aarongreen): Determine processor cryptographic capabilities. See
    // magenta/third_party/ulib/musl/include/sys/auxv.h. Without a mechanism
    // similar to AT_HWCAP, there's currently nothing discoverable.
    return;
}

#endif // OPENSSL_AARCH64 && !OPENSSL_STATIC_ARMCAP
