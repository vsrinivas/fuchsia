// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_H_

#include <lib/arch/ticks.h>
#include <stddef.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/type_traits.h>
#include <phys/arch/arch-handoff.h>

// This holds arch::EarlyTicks timestamps collected by physboot before the
// kernel proper is cognizant.  Once the platform timer hardware is set up for
// real, platform_convert_early_ticks translates these values into zx_ticks_t
// values that can be published as kcounters and then converted to actual time
// units in userland via zx_ticks_per_second().
//
// platform_convert_early_ticks returns zero if arch::EarlyTicks samples cannot
// be accurately converted to zx_ticks_t.  This can happen on suboptimal x86
// hardware, where the early samples are in TSC but the platform timer decides
// that a synchronized and monotonic TSC is not available on the machine.
class PhysBootTimes {
 public:
  // These are various time points sampled during physboot's work.
  // kernel/top/handoff.cc has a kcounter corresponding to each of these.
  // When a new time point is added here, a new kcounter must be added
  // there to make that sample visible anywhere.
  enum Index : size_t {
    kZbiEntry,         // ZBI entry from boot loader.
    kPhysSetup,        // Earliest/arch-specific phys setup (e.g. paging).
    kDecompressStart,  // Begin decompression.
    kDecompressEnd,    // STORAGE_KERNEL decompressed.
    kZbiDone,          // ZBI items have been ingested.
    kCount
  };

  constexpr arch::EarlyTicks Get(Index i) const { return timestamps_[i]; }

  constexpr void Set(Index i, arch::EarlyTicks ts) { timestamps_[i] = ts; }

  void SampleNow(Index i) { Set(i, arch::EarlyTicks::Get()); }

 private:
  arch::EarlyTicks timestamps_[kCount] = {};
};

// This holds (or points to) everything that is handed off from physboot to the
// kernel proper at boot time.
struct PhysHandoff {
  constexpr bool Valid() const { return magic == kMagic; }

  static constexpr uint64_t kMagic = 0xfeedfaceb002da2a;

  const uint64_t magic = kMagic;

  // Architecture-specific content.
  ArchPhysHandoff arch_handoff;
  static_assert(ktl::is_default_constructible_v<ArchPhysHandoff>);

  PhysBootTimes times;
  static_assert(ktl::is_default_constructible_v<PhysBootTimes>);

  // Physical address of the data ZBI.
  uint64_t zbi = 0;

  // ZBI_TYPE_PLATFORM_ID payload.
  ktl::optional<zbi_platform_id_t> platform_id;
};

static_assert(ktl::is_default_constructible_v<PhysHandoff>);

extern PhysHandoff* gPhysHandoff;

#ifdef _KERNEL

// These functions relate to PhysHandoff but exist only in the kernel proper.

#include <sys/types.h>

// Called as soon as the physmap is available to set the gPhysHandoff pointer.
void HandoffFromPhys(paddr_t handoff_paddr);

// This can be used after HandoffFromPhys and before the ZBI is handed off to
// userboot at the very end of kernel initialization code.  Userboot calls it
// with true to ensure no later calls will succeed.
ktl::span<ktl::byte> ZbiInPhysmap(bool own = false);

#endif  // _KERNEL

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_H_
