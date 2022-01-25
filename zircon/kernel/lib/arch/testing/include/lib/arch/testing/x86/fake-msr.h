// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_MSR_H_
#define ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_MSR_H_

#include <lib/arch/x86/msr.h>
#include <lib/fit/function.h>

#include <memory>

#include <fbl/intrusive_hash_table.h>
#include <hwreg/internal.h>

namespace arch::testing {

// FakeMsrIo is a fake analogue to hwreg::X86MsrIo, which may be provided in
// its place in tests - in the kernel and on host.
//
// FakeMsrIo is immovable and non-copyable; it is expected to be passed
// around by reference.
class FakeMsrIo {
 public:
  // Represents a system side-effect from MSR access. Called on either read or
  // write, the callback is passed the accessed MSR's address and a reference
  // to its value. Example usages include resetting an MSR back to its default
  // value on write or incrementing a fake timestamp counter on RDTSC read.
  using IoCallback = fit::inline_function<void(X86Msr, uint64_t&)>;

  // A canonical no-op IoCallback.
  static constexpr auto kNoSideEffects = [](X86Msr msr, uint64_t& value) {};

  // Gives a FakeMsrIo with no side-effects, which is little more than a
  // glorified map of MSR address to value.
  FakeMsrIo() = default;

  // Constructs a FakeMsrIo with particular on-write and on-read `IoCallback`s.
  FakeMsrIo(IoCallback on_write, IoCallback on_read)
      : on_write_(std::move(on_write)), on_read_(std::move(on_read)) {}

  // Populate must be called with a particular MSR by a test author before that
  // same MSR can be used with Read() and Write(). A call will not result in
  // any side-effects.
  FakeMsrIo& Populate(X86Msr msr, uint64_t initial_value);

  // Reads the stored MSR value without side-effects. The MSR must have been
  // `Populate`d before this can be called on it.
  uint64_t Peek(X86Msr msr) const;

  // Implements an I/O provider's `Write()` method. An MSR must have been
  // `Populate`d before this can be called on it.
  template <typename IntType>
  void Write(IntType value, uint32_t msr) {
    static_assert(std::is_same_v<IntType, uint64_t>);
    Write(static_cast<X86Msr>(msr), value);
  }

  // Implements an I/O provider's `Read()` method. An MSR must have been
  // `Populate`d before this can be called on it.
  template <typename IntType>
  IntType Read(uint32_t msr) {
    static_assert(std::is_same_v<IntType, uint64_t>);
    return Read(static_cast<X86Msr>(msr));
  }

 private:
  // An intrusive data structure wrapping a uint64_t MSR value, to be stored in
  // a fbl::HashTable.
  //
  // This would be simpler using something more along the lines of
  // std::map<X86Msr, uint64_t>, but we need to support in-kernel testing
  // where only fbl containers are available.
  struct Hashable : public fbl::SinglyLinkedListable<std::unique_ptr<Hashable>> {
    // Required to instantiate fbl::DefaultKeyedObjectTraits.
    X86Msr GetKey() const { return msr_; }

    // Required to instantiate fbl::DefaultHashTraits.
    static size_t GetHash(X86Msr key) { return static_cast<size_t>(key); }

    X86Msr msr_;
    uint64_t value_;
  };

  void Write(X86Msr msr, uint64_t value);

  uint64_t Read(X86Msr msr);

  const IoCallback on_write_ = kNoSideEffects;
  const IoCallback on_read_ = kNoSideEffects;
  fbl::HashTable<X86Msr, std::unique_ptr<Hashable>> map_;
};

}  // namespace arch::testing

#endif  // ZIRCON_KERNEL_LIB_ARCH_TESTING_INCLUDE_LIB_ARCH_TESTING_X86_FAKE_MSR_H_
