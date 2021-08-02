// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_JTRACE_CONFIG_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_JTRACE_CONFIG_H_

#include <stdint.h>

#include <kernel/persistent_ram.h>

#ifndef JTRACE_TARGET_BUFFER_SIZE
#error JTRACE_TARGET_BUFFER_SIZE must be defined by the build system!
#endif

#ifndef JTRACE_LAST_ENTRY_STORAGE
#define JTRACE_LAST_ENTRY_STORAGE 0
#endif

#ifndef JTRACE_IS_PERSISTENT
#define JTRACE_IS_PERSISTENT false
#endif

#ifndef JTRACE_USE_LARGE_ENTRIES
#define JTRACE_USE_LARGE_ENTRIES false
#endif

namespace jtrace {

// enum-class style bools we will use for configuring our jtrace implementation.
enum class UseLargeEntries { No = 0, Yes };
enum class IsPersistent { No = 0, Yes };

}  // namespace jtrace

constexpr size_t kJTraceTargetBufferSize = JTRACE_TARGET_BUFFER_SIZE;
constexpr size_t kJTraceLastEntryStorage = JTRACE_LAST_ENTRY_STORAGE;
constexpr jtrace::IsPersistent kJTraceIsPersistent =
    JTRACE_IS_PERSISTENT ? jtrace::IsPersistent::Yes : jtrace::IsPersistent::No;
constexpr jtrace::UseLargeEntries kJTraceUseLargeEntries =
    JTRACE_USE_LARGE_ENTRIES ? jtrace::UseLargeEntries::Yes : jtrace::UseLargeEntries::No;
constexpr size_t kJTraceTargetPersistentBufferSize =
    (kJTraceIsPersistent == jtrace::IsPersistent::Yes) ? kJTraceTargetBufferSize : 0;

static_assert(
    (kJTraceIsPersistent == jtrace::IsPersistent::No) ||
        ((kJTraceTargetBufferSize % kPersistentRamAllocationGranularity) == 0),
    "Minimum reserved persistent debug trace size must be a multiple of the persistent RAM "
    "allocation granularity");

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_JTRACE_CONFIG_H_
