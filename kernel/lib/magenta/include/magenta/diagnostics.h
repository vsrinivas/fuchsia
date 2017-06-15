// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <magenta/syscalls/object.h>
#include <mxtl/ref_ptr.h>

class ProcessDispatcher;
class VmAspace;

// Walks the VmAspace and writes entries that describe it into |maps|, which
// must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
status_t GetVmAspaceMaps(mxtl::RefPtr<VmAspace> aspace,
                         user_ptr<mx_info_maps_t> maps, size_t max,
                         size_t* actual, size_t* available);

// Walks the VmAspace and writes entries that describe its mapped VMOs into
// |vmos|, which must point to enough memory for |max| entries. The number of
// entries written is returned via |actual|, and the number entries that could
// have been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
status_t GetVmAspaceVmos(mxtl::RefPtr<VmAspace> aspace,
                         user_ptr<mx_info_vmo_t> vmos, size_t max,
                         size_t* actual, size_t* available);

// For every VMO in the process's handle table, writes an entry into |vmos|,
// which must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
status_t GetProcessVmosViaHandles(ProcessDispatcher* process,
                                  user_ptr<mx_info_vmo_t> vmos, size_t max,
                                  size_t* actual, size_t* available);
