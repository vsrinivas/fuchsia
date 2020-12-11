// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_CPUID_H_
#define ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_CPUID_H_

#include <lib/arch/x86/cpuid.h>

#include <memory>

#include <fbl/intrusive_hash_table.h>

namespace arch::testing {

// FakeCpuidIo is a fake analogue to arch::BootCpuidIo, which may be provided
// in its place for tests - in the kernel and on host - for logic templated
// on any type the interface contract of the latter. Using `Populate`, test
// authors can provide dummy data for specific (sub)leaves.
//
// FakeCpuidIo is immovable and non-copyable; it is expected to be passed
// around by const reference.
class FakeCpuidIo {
 public:
  // Returns the cached CpuidIo object corresponding to the particular CPUID
  // register type. This method mirrors that of arch::BootCpuidIo and is
  // required to meet its interface contract.
  template <typename CpuidValue>
  const CpuidIo* Get() const {
    return Get(CpuidValue::kLeaf, CpuidValue::kSubleaf);
  }

  // A convenience method to directly read a particular CPUID register type in
  // consultation with the associated cached CpuidIo objects. This method
  // mirrors that of arch::BootCpuidIo and is required to meet its interface
  // contract.
  template <typename CpuidValue>
  auto Read() const {
    return CpuidValue::Get().ReadFrom(Get<CpuidValue>());
  }

  // Provides dummy data for a given return register of a particular
  // (leaf, subleaf), where `reg` must be one of the `arch::CpuidIo::Register`
  // values. Subsequent calls can overwrite previously populated data.
  FakeCpuidIo& Populate(uint32_t leaf, uint32_t subleaf, uint32_t reg, uint32_t value);

 private:
  // An intrusive data structure wrapping a CpuidIo object, required to store
  // in a fbl::HashTable.
  //
  // This would be simple using something more along the lines of
  // std::map<std::pair<uint32_t, uint32_t>, CpuidIo>, but we needs to
  // support in-kernel testing where only fbl containers are available.
  struct Hashable : public fbl::SinglyLinkedListable<std::unique_ptr<Hashable>> {
    // Required to instantiate fbl::DefaultKeyedObjectTraits.
    uint64_t GetKey() const { return key_; }

    // Required to instantiate fbl::DefaultHashTraits.
    static size_t GetHash(uint64_t key) { return static_cast<size_t>(key); }

    uint64_t key_;
    CpuidIo cpuid_;
  };

  static uint64_t Key(uint32_t leaf, uint32_t subleaf) {
    return (static_cast<uint64_t>(subleaf) << 32) | static_cast<uint64_t>(leaf);
  }

  const CpuidIo* Get(uint32_t leaf, uint32_t subleaf) const;

  const CpuidIo empty_ = {};
  fbl::HashTable<uint64_t, std::unique_ptr<Hashable>> map_;
};

}  // namespace arch::testing

#endif  // ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_CPUID_H_
