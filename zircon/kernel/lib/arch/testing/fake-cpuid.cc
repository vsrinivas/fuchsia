// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <zircon/assert.h>

#include <fbl/alloc_checker.h>

namespace arch::testing {

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
