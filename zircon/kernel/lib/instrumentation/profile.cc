// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <lib/llvm-profdata/llvm-profdata.h>
#include <lib/version.h>
#include <stdint.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <vm/vm_object_paged.h>

#include "kernel-mapped-vmo.h"
#include "private.h"

namespace {

constexpr ktl::string_view kVmoName = "data/zircon.elf.profraw";

// This holds the pinned mapping of the live-updated counters.
KernelMappedVmo gProfdataCounters;

}  // namespace

InstrumentationDataVmo LlvmProfdataVmo() {
  LlvmProfdata profdata;
  profdata.Init(ElfBuildId());
  if (profdata.size_bytes() == 0) {
    return {};
  }

  // Create a VMO to hold the whole profdata dump.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, profdata.size_bytes(), &vmo);
  ZX_ASSERT(status == ZX_OK);

  // First fill in just the fixed data, by mapping the whole VMO into the
  // kernel address space.  Then let the mapping and pinning be cleaned up,
  // since we don't need the whole thing mapped into the kernel at runtime.
  {
    KernelMappedVmo mapped_vmo;
    status = mapped_vmo.Init(vmo, 0, profdata.size_bytes(), "llvm-profdata-setup");
    ZX_ASSERT(status == ZX_OK);
    ktl::span<ktl::byte> mapped_data{
        reinterpret_cast<ktl::byte*>(mapped_vmo.base()),
        mapped_vmo.size(),
    };
    profdata.WriteFixedData(mapped_data);
  }

  // Now map in just the pages holding the counters.  This mapping will be kept
  // alive permanently so the live counters can be updated through it.
  const uint64_t map_offset = ROUNDDOWN(profdata.counters_offset(), PAGE_SIZE);
  const size_t map_size =
      ROUNDUP_PAGE_SIZE(profdata.counters_offset() + profdata.counters_size_bytes()) - map_offset;
  status = gProfdataCounters.Init(ktl::move(vmo), map_offset, map_size, "llvm-profdata-counters");
  ZX_ASSERT(status == ZX_OK);
  ktl::span<ktl::byte> counters{
      reinterpret_cast<ktl::byte*>(profdata.counters_offset() - map_offset +
                                   gProfdataCounters.base()),
      profdata.counters_size_bytes(),
  };

  // Counts up to this point have collected in global variable space.
  // Copy those counters into the mapped VMO data.
  profdata.CopyCounters(counters);

  // Switch instrumented code over to updating the mapped VMO data in place.
  // From this point on, the kernel's VMO mapping is used by all instrumented
  // code and must be kept valid and pinned.
  //
  // TODO(mcgrathr): We could theoretically decommit the global data pages
  // after this to recover that RAM.  That part of the kernel's global data
  // area should never be accessed again.
  LlvmProfdata::UseCounters(counters);

  return {
      .announce = LlvmProfdata::kAnnounce,
      .sink_name = LlvmProfdata::kDataSinkName,
      .handle = gProfdataCounters.Publish(kVmoName, profdata.size_bytes()),
  };
}
