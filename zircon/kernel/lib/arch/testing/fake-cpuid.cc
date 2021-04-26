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
    case X86Microprocessor::kIntelAtom330: {
#include "data/cpuid/intel-atom-330.inc"
      break;
    }
    case X86Microprocessor::kIntelAtomD510: {
#include "data/cpuid/intel-atom-d510.inc"
      break;
    }
    case X86Microprocessor::kIntelAtomX5_Z8350: {
#include "data/cpuid/intel-atom-x5-z8350.inc"
      break;
    }
    case X86Microprocessor::kIntelCeleron3855u: {
#include "data/cpuid/intel-celeron-3855u.inc"
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
    case X86Microprocessor::kIntelCoreI5_7300u: {
#include "data/cpuid/intel-core-i5-7300u.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI7_2600k: {
#include "data/cpuid/intel-core-i7-2600k.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI7_6500u: {
#include "data/cpuid/intel-core-i7-6500u.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreI7_6700k: {
#include "data/cpuid/intel-core-i7-6700k.inc"
      break;
    }
    case X86Microprocessor::kIntelCoreM3_7y30: {
#include "data/cpuid/intel-core-m3-7y30.inc"
      break;
    }
    case X86Microprocessor::kIntelPentiumN4200: {
#include "data/cpuid/intel-pentium-n4200.inc"
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
    case X86Microprocessor::kIntelXeonE5_2690_V4: {
#include "data/cpuid/intel-xeon-e5-2690-v4.inc"
      break;
    }
    case X86Microprocessor::kAmdA10_7870k: {
#include "data/cpuid/amd-a10-7870k.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen5_1500x: {
#include "data/cpuid/amd-ryzen-5-1500x.inc"
      break;
    }
    case X86Microprocessor::kAmdRyzen7_1700: {
#include "data/cpuid/amd-ryzen-7-1700.inc"
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
    case X86Microprocessor::kAmdRyzenThreadripper2970wx: {
#include "data/cpuid/amd-ryzen-threadripper-2970wx.inc"
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
    ZX_ASSERT(ac.check());
    hashable->key_ = key;
    populate(hashable->cpuid_);
    map_.insert(std::move(hashable));
  } else {
    populate(it->cpuid_);
  }
  return *this;
}

FakeCpuidIo& FakeCpuidIo::Populate(uint32_t leaf, uint32_t subleaf, CpuidIo::Register reg,
                                   uint32_t value) {
  ZX_ASSERT(reg <= CpuidIo::Register::kEdx);
  const auto key = Key(leaf, subleaf);
  if (auto it = map_.find(key); it == map_.end()) {
    fbl::AllocChecker ac;
    std::unique_ptr<Hashable> hashable(new (&ac) Hashable{});
    ZX_ASSERT(ac.check());
    hashable->key_ = key;
    hashable->cpuid_.values_[reg] = value;
    map_.insert(std::move(hashable));
  } else {
    it->cpuid_.values_[reg] = value;
  }
  return *this;
}

}  // namespace arch::testing
