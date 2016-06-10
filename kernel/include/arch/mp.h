// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <kernel/mp.h>

/* send inter processor interrupt, if supported */
status_t arch_mp_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi);

/* Prepare for CPU unplug.  The platform may want to shift
 * around external interrupts at this time. */
status_t platform_mp_prep_cpu_unplug(uint cpu_id);

/* shutdown the specified CPU.  called after it is no longer
 * being scheduled on.  */
status_t platform_mp_cpu_unplug(uint cpu_id);

/* Should be invoked by platform_mp_prep_cpu_unplug to ask the
 * arch to do whatever it needs to do to stop the CPU */
status_t arch_mp_prep_cpu_unplug(uint cpu_id);

/* Should be invoked by platform_mp_cpu_unplug to ask the
 * arch to do whatever it needs to do to stop the CPU */
status_t arch_mp_cpu_unplug(uint cpu_id);

void arch_mp_init_percpu(void);
