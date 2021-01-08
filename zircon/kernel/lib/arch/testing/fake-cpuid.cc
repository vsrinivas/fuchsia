// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <zircon/assert.h>

#include <fbl/alloc_checker.h>

namespace arch::testing {

FakeCpuidIo::FakeCpuidIo(X86Microprocessor microprocessor) {
#define DEFINE_CPUID_VALUES(leaf, subleaf, eax, ebx, ecx, edx) \
  Populate(leaf, subleaf, eax, ebx, ecx, edx);

  switch (microprocessor) {
    case X86Microprocessor::kIntelAtomD510: {
#include "data/cpuid/intel-atom-d510.inc"
      break;
    }
    case X86Microprocessor::kIntelCore2_6300: {
#include "data/cpuid/intel-core2-6300.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI3_3240: {
#include "data/cpuid/intel-core-i3-3240.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI3_6100: {
#include "data/cpuid/intel-core-i3-6100.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI7_2600k: {
#include "data/cpuid/intel-core-i7-2600k.inc"
      break;
    }
    case X86Microprocessor::kIntelXeonE5520: {
#include "data/cpuid/intel-xeon-e5520.inc"
      break;
    }
    case X86Microprocessor::kIntelXeonE5_2690_V3: {
#include "data/cpuid/intel-xeon-e5-2690-v3.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen7_2700x: {
#include "data/cpuid/amd-ryzen-7-2700x.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen9_3950x: {
#include "data/cpuid/amd-ryzen-9-3950x.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen9_3950xVirtualBoxHyperv: {
#include "data/cpuid/amd-ryzen-9-3950x-virtualbox-hyperv.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen9_3950xVirtualBoxKvm: {
#include "data/cpuid/amd-ryzen-9-3950x-virtualbox-kvm.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen9_3950xVmware: {
#include "data/cpuid/amd-ryzen-9-3950x-vmware.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen9_3950xWsl2: {
#include "data/cpuid/amd-ryzen-9-3950x-wsl2.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzenThreadripper1950x: {
#include "data/cpuid/amd-ryzen-threadripper-1950x.inc"
      break;
    }
  };

#undef DEFINE_CPUID_VALUES
}

const CpuidIo* FakeCpuidIo::Get(uint32_t leaf, uint32_t subleaf) const {
  const auto it = map_.find(Key(leaf, subleaf));
  return it == map_.end() ? &empty_ : &(it->cpuid_);
}

FakeCpuidIo& FakeCpuidIo::Populate(uint32_t leaf, uint32_t subleaf, uint32_t eax, uint32_t ebx,
                                   uint32_t ecx, uint32_t edx) {
  auto populate = [&eax, &ebx, &ecx, &edx](CpuidIo& io) {
    io.values_[CpuidIo::kEax] = eax;
    io.values_[CpuidIo::kEbx] = ebx;
    io.values_[CpuidIo::kEcx] = ecx;
    io.values_[CpuidIo::kEdx] = edx;
  };

  const auto key = Key(leaf, subleaf);
  if (auto it = map_.find(key); it == map_.end()) {
    fbl::AllocChecker ac;
    std::unique_ptr<Hashable> hashable(new (&ac) Hashable{});
    ZX_DEBUG_ASSERT(ac.check());
    hashable->key_ = key;
    populate(hashable->cpuid_);
    map_.insert(std::move(hashable));
  } else {
    populate(it->cpuid_);
  }
  return *this;
}

}  // namespace arch::testing
