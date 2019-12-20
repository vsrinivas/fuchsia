// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/instrumentation/vmo.h>

#include <cstring>

#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_paged.h>

namespace {

// These are defined by the linker script.  When there is no such
// instrumentation data in this kernel build, they're equal.
extern "C" const uint8_t __llvm_profile_start[], __llvm_profile_end[];
extern "C" const uint8_t __llvm_profile_vmo_end[];
extern "C" const uint8_t __sancov_pc_table[], __sancov_pc_table_end[];
extern "C" const uint8_t __sancov_pc_table_vmo_end[];
extern "C" const uint8_t __sancov_pc_counts[], __sancov_pc_counts_end[];
extern "C" const uint8_t __sancov_pc_counts_vmo_end[];

constexpr struct {
  const char* announce;
  const char* sink_name;
  const char* vmo_name;
  const uint8_t* start;
  const uint8_t* end;
  const uint8_t* vmo_end;
  size_t scale;
  const char* units;
} kKinds[] = {
    // LLVM profile data.  When not compiled in, this will be a zero-length
    // anonymous VMO and userland will just ignore it.  But it's simpler to
    // keep the number of VMOs fixed in the ABI with userboot because the
    // way the build works, the userboot build is independent of different
    // kernel variants that might have things enabled or disabled.
    {"LLVM Profile", "llvm-profile", "data/zircon.elf.profdata",
     // Linker-generated symbols.
     __llvm_profile_start, __llvm_profile_end, __llvm_profile_vmo_end,
     // Units.
     1, "bytes"},

    // -fsanitizer-coverage=trace-pc-guard data.  Same story.
    {"SanitizerCoverage", "sancov",
     // The sancov tool matches "<binaryname>" to "<binaryname>.%u.sancov".
     "data/zircon.elf.1.sancov",
     // Linker-generated symbols.
     __sancov_pc_table, __sancov_pc_table_end, __sancov_pc_table_vmo_end,
     // Units.
     sizeof(uintptr_t), "PCs"},
    {"SanitizerCoverage Counts", "sancov-counts",
     // This follows the sancov PCs file name just for consistency.
     "data/zircon.elf.1.sancov-counts",
     // Linker-generated symbols.
     __sancov_pc_counts, __sancov_pc_counts_end, __sancov_pc_counts_vmo_end,
     // Units.
     sizeof(uint64_t), "counters"},
};

}  // namespace

decltype(InstrumentationData::instances_) InstrumentationData::instances_;

zx_status_t InstrumentationData::Create() {
  const auto& k = kKinds[which()];
  return VmObjectPaged::CreateFromWiredPages(k.start, k.vmo_end - k.start, false, &vmo_);
}

zx_status_t InstrumentationData::GetVmo(Handle** handle) {
  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> new_handle;
  zx_status_t status = VmObjectDispatcher::Create(vmo_, &new_handle, &rights);
  if (status == ZX_OK) {
    *handle = Handle::Make(ktl::move(new_handle), rights & ~ZX_RIGHT_WRITE).release();
  }
  return status;
}

void InstrumentationData::Publish() {
  if (vmo_->size() == 0) {
    // The empty VMO doesn't need to be kept alive.
    vmo_.reset();
  } else {
    const auto& k = kKinds[which()];

    // Set the name to expose the meaning of the VMO to userland.
    vmo_->set_name(k.vmo_name, strlen(k.vmo_name));

    // Log the name that goes with the VMO.
    printf("%s: {{{dumpfile:%s:%s}}} maximum %zu %s.\n", k.announce, k.sink_name, k.vmo_name,
           (k.end - k.start) / k.scale, k.units);
  }
}

zx_status_t InstrumentationData::GetVmos(Handle* handles[]) {
  for (auto& instance : instances_) {
    zx_status_t status = instance.Create();
    if (status == ZX_OK) {
      status = instance.GetVmo(&handles[instance.which()]);
    }
    if (status != ZX_OK) {
      return status;
    }
    instance.Publish();
  }
  return ZX_OK;
}
