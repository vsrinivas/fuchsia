// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_

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

/* If len == 0, return the length of the last crashlog (or 0 if none).
 * Otherwise call func() to return the last crashlog to the caller,
 * returning the length the last crashlog.
 *
 * func() may be called as many times as necessary (adjusting off)
 * to return the crashlog in segments.  There will not be gaps,
 * but the individual segments may range from 1 byte to the full
 * length requested, depending on the limitations of the underlying
 * storage model.
 */
extern size_t (*platform_recover_crashlog)(size_t len, void* cookie,
                                           void (*func)(const void* data, size_t off, size_t len,
                                                        void* cookie));

/* Either enable or disable periodic updates of the crashlog uptime. */
extern void (*platform_enable_crashlog_uptime_updates)(bool enabled);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
