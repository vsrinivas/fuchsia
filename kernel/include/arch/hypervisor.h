// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#if ARCH_ARM64
#include <arch/arm64/hypervisor.h>
#endif

#if ARCH_X86_64
#include <arch/x86/hypervisor.h>
#endif

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

__BEGIN_CDECLS

/* Create a hypervisor context.
 * This setups up the CPUs to allow a hypervisor to be run.
 */
mx_status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context);

/* Create a guest context.
 * This creates the structures to allow a guest to be run.
 */
mx_status_t arch_guest_create(mxtl::unique_ptr<GuestContext>* context);

/* Start a guest within a guest context.
 * This launches a guest at the given entry point, within a guest context.
 */
mx_status_t arch_guest_start(const mxtl::unique_ptr<GuestContext>& context, uintptr_t entry,
                             uintptr_t stack);

__END_CDECLS
