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

// Abstraction around writing zx_info_vmo_t's to user space. As there are multiple versions of the
// zx_info_vmo_t struct and we generally just want to produce the most recent one specializations
// of this class deal with converting from the most recent representation to the version requested
// by the user.
// Since conversion may need to occur element by element this provides no multi-element array writes
// like the regular user_copy interface.
class VmoInfoWriter {
 public:
  VmoInfoWriter() = default;
  virtual ~VmoInfoWriter() {}
  // Writes a single zx_info_vmo_t at the given element offset. Return values are same as
  // user_ptr::copy_to_user
  virtual zx_status_t Write(const zx_info_vmo_t& vmo, size_t offset) = 0;
  // Same as Write, except is the no faulting variant and returns the complete capture result type.
  virtual UserCopyCaptureFaultsResult WriteCaptureFaults(const zx_info_vmo_t& vmo,
                                                         size_t offset) = 0;
  // Increases the base offset such that Writes to offset 0 write to this offset.
  virtual void AddOffset(size_t offset) = 0;
};

// Walks the VmAspace and writes entries that describe it into |maps|, which
// must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
// |current_aspace| refers to the current active aspace for which |maps| is a pointer for, and
// |target_aspace| is the aspace that is to be enumerated.
zx_status_t GetVmAspaceMaps(VmAspace* current_aspace, fbl::RefPtr<VmAspace> target_aspace,
                            user_out_ptr<zx_info_maps_t> maps, size_t max, size_t* actual,
                            size_t* available);

// Walks the VmAspace and writes entries that describe its mapped VMOs into
// |vmos|, which must point to enough memory for |max| entries. The number of
// entries written is returned via |actual|, and the number entries that could
// have been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
// |current_aspace| refers to the current active aspace for which |vmos| is a pointer for, and
// |target_aspace| is the aspace that is to be enumerated.
zx_status_t GetVmAspaceVmos(VmAspace* current_aspace, fbl::RefPtr<VmAspace> target_aspace,
                            VmoInfoWriter& vmos, size_t max, size_t* actual, size_t* available);

// For every VMO in the process's handle table, writes an entry into |vmos|,
// which must point to enough memory for |max| entries. The number of entries
// written is returned via |actual|, and the number entries that could have
// been written is returned via |available|.
// NOTE: Code outside of the syscall layer should not typically know about
// user_ptrs; do not use this pattern as an example.
zx_status_t GetProcessVmos(ProcessDispatcher* process, VmoInfoWriter& vmos, size_t max,
                           size_t* actual, size_t* available);

// Prints (with the supplied prefix) the number of mapped, committed bytes for
// each process in the system whose page count > |min_pages|. Does not take
// sharing into account, and does not count unmapped VMOs.
void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_DIAGNOSTICS_H_
