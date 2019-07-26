// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_DIAGNOSTICS_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_DIAGNOSTICS_H_

#include <lib/user_copy/user_ptr.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <object/process_dispatcher.h>

class VmAspace;

// Walks the VmAspace and writes entries that describe it into |maps|, which
// must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetVmAspaceMaps(fbl::RefPtr<VmAspace> aspace, user_out_ptr<zx_info_maps_t> maps,
                            size_t max, size_t* actual, size_t* available);

// Walks the VmAspace and writes entries that describe its mapped VMOs into
// |vmos|, which must point to enough memory for |max| entries. The number of
// entries written is returned via |actual|, and the number entries that could
// have been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetVmAspaceVmos(fbl::RefPtr<VmAspace> aspace, user_out_ptr<zx_info_vmo_t> vmos,
                            size_t max, size_t* actual, size_t* available);

// For every VMO in the process's handle table, writes an entry into |vmos|,
// which must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
// Requires that the caller hold the |process->handle_table_lock()| so it can
// call ForEachHandleLocked. Pushing locking into the caller allows the caller
// to resolve any lock ordering issues.
zx_status_t GetProcessVmosLocked(ProcessDispatcher* process, user_out_ptr<zx_info_vmo_t> vmos,
                                 size_t max, size_t* actual, size_t* available)
    TA_REQ(process->handle_table_lock());

// Prints (with the supplied prefix) the number of mapped, committed bytes for
// each process in the system whose page count > |min_pages|. Does not take
// sharing into account, and does not count unmapped VMOs.
void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_DIAGNOSTICS_H_
