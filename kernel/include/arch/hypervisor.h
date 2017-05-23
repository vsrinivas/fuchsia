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
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

typedef struct mx_guest_gpr mx_guest_gpr_t;

class FifoDispatcher;
class VmObject;

/* Create a hypervisor context.
 * This setups up the CPUs to allow a hypervisor to be run.
 */
status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context);

/* Create a guest context.
 * This creates the structures to allow a guest to be run.
 */
status_t arch_guest_create(mxtl::RefPtr<VmObject> phys_mem,
                           mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                           mxtl::unique_ptr<GuestContext>* context);

/* Enter a guest context.
 */
status_t arch_guest_enter(const mxtl::unique_ptr<GuestContext>& context);

/* Create a guest context.
 */
status_t arch_guest_mem_trap(const mxtl::unique_ptr<GuestContext>& context, vaddr_t guest_paddr,
                             size_t size);

/* Set general purpose registers of a guest context.
 */
status_t arch_guest_set_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            const mx_guest_gpr_t& guest_gpr);

/* Get general purpose registers of a guest context.
 */
status_t arch_guest_get_gpr(const mxtl::unique_ptr<GuestContext>& context,
                            mx_guest_gpr_t* guest_gpr);

/* Set the instruction pointer of a guest context.
 */
status_t arch_guest_set_ip(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_ip);
