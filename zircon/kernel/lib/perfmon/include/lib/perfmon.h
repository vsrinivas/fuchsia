// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_perfmon.h>
#include <err.h>
#include <fbl/ref_ptr.h>
#include <kernel/align.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

using PmuEventId = perfmon_event_id_t;

// The functions performing |mtrace_control()| operations.

// Perform MTRACE_PERFMON_GET_PROPERTIES: Store PMU properties in |state|.
zx_status_t arch_perfmon_get_properties(ArchPmuProperties* state);

// Perform MTRACE_PERFMON_INIT: Prepare for a data collection run.
zx_status_t arch_perfmon_init();

// Perform MTRACE_PERFMON_ASSIGN_BUFFER: Assign |vmo| as the buffer for |cpu|.
// The VMO is not mapped into kernel space yet, that is done later by
// |arch_perfmon_start()|.
zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo);

// Perform MTRACE_PERFMON_STAGE_CONFIG: Record |config| as the configuration
// of data to be collected.
zx_status_t arch_perfmon_stage_config(ArchPmuConfig* config);

// Perform MTRACE_PERFMON_START: Initialize PMU registers, map VMOs into
// kernel space, and turn on PMU interrupt if necessary.
zx_status_t arch_perfmon_start();

// Perform MTRACE_PERFMON_STOP: Stop data collection, including collecting
// the final values of the counters and unmapping the VMOs.
zx_status_t arch_perfmon_stop();

// Perform MTRACE_PERFMON_FINI: Terminate data collection, reset all PMU
// registers.
zx_status_t arch_perfmon_fini();
