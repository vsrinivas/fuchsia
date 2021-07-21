// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_INCLUDE_LIB_PERSISTENT_DEBUGLOG_H_
#define ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_INCLUDE_LIB_PERSISTENT_DEBUGLOG_H_

#include <stdint.h>

#include <kernel/persistent_ram.h>
#include <ktl/limits.h>
#include <ktl/string_view.h>

#ifndef TARGET_PERSISTENT_DEBUGLOG_SIZE
#define TARGET_PERSISTENT_DEBUGLOG_SIZE 0
#endif

static constexpr size_t kTargetPersistentDebugLogSize = TARGET_PERSISTENT_DEBUGLOG_SIZE;
static_assert((kTargetPersistentDebugLogSize % kPersistentRamAllocationGranularity) == 0,
              "Minimum reserved crashlog size must be a multiple of the persistent RAM allocation "
              "granularity");
static_assert(kTargetPersistentDebugLogSize <= ktl::numeric_limits<uint32_t>::max());

// Called once from lib/debuglog during _very_ early init.
void persistent_dlog_init_early();

// Sets the virtual address of where to store the persistent, assuming that we
// have one.  This needs to happen early in boot, usually during ZBI header
// processing, before we start up the secondary CPUs.
void persistent_dlog_set_location(void* vaddr, size_t len);

// Writes a string to the persistent dlog, if enabled.  Otherwise, this is a
// no-op.
void persistent_dlog_write(const ktl::string_view str);

// Invalidates the state of the persistent dlog.  This gets called every time we
// gracefully reboot, so that we don't end up recovering a dlog after reboot and
// end up producing an unnecessary crashlog.
void persistent_dlog_invalidate();

// Fetch a string view which references the recovered crashlog (if any).
// size written.
ktl::string_view persistent_dlog_get_recovered_log();

#endif  // ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_INCLUDE_LIB_PERSISTENT_DEBUGLOG_H_
