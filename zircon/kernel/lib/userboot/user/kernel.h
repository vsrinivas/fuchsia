// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This file specifies the private ABI shared between userboot and the kernel.
// That is, the contents of the message sent on userboot's bootstrap channel.

#include <cstdint>
#include <lib/vdso-variants.h>

namespace userboot {

// The data of the bootstrap message is the kernel command line,
// as a sequence of '\0'-terminated words followed by a final '\0'.
// This is its maximum size.
constexpr uint32_t kCmdlineMax = 4096;

// The handles in the bootstrap message are as follows:
enum HandleIndex : uint32_t {
    // These describe userboot itself.
    kProcSelf,
    kVmarRootSelf,

    // Essential job and resource handles.
    kRootJob,
    kRootResource,

    // Essential VMO handles.
    kZbi,

    kFirstVdso,
    kLastVdso = kFirstVdso + static_cast<uint32_t>(VdsoVariant::COUNT) - 1,

    // The remainder are VMO handles that userboot doesn't care about.
    // They just get passed along to userland to be recognized by ZX_PROP_NAME.
    kFirstKernelFile,

    kCrashlog = kFirstKernelFile,
    kCounterNames,
    kCounters,
#if ENABLE_ENTROPY_COLLECTOR_TEST
    kEntropyTestData,
#endif

    kHandleCount
};

} // namespace userboot
