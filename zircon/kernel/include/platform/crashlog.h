// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_

#include <stdio.h>
#include <sys/types.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

void platform_set_ram_crashlog_location(paddr_t phys, size_t len);
bool platform_has_ram_crashlog();

/* Stash the crashlog somewhere platform-specific that allows
 * for recovery after reboot.  This will only be called out
 * of the panic() handling path on the way to reboot, and is
 * not necessarily safe to be called from any other state.
 *
 * Calling with a NULL log returns the maximum supported size.
 * It is safe to query the size at any time after boot.  If the
 * return is 0, no crashlog recovery is supported.
 */
extern void (*platform_stow_crashlog)(zircon_crash_reason_t reason, const void* log, size_t len);

/* Recover the crashlog, fprintf'ing its contents into the FILE |tgt|
 * provided by the caller, then return the length of the recovered
 * crashlog.
 *
 * It is safe to call this function more than once.  Users may compute
 * the length of the crashlog without rendering it by passing nullptr
 * for |tgt|.  The length of the rendered log is guaranteed to stay
 * constant between calls.
 *
 */
extern size_t (*platform_recover_crashlog)(FILE* tgt);

/* Either enable or disable periodic updates of the crashlog uptime. */
extern void (*platform_enable_crashlog_uptime_updates)(bool enabled);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
