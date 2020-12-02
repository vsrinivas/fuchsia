// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTERS_H_
#define ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTERS_H_

#include <lib/special-sections/special-sections.h>

#include <arch/ops.h>
#include <kernel/atomic.h>
#include <kernel/percpu.h>

#include "counter-vmo-abi.h"

// Kernel counters are a facility designed to help field diagnostics and
// to help devs properly dimension the load/clients/size of the kernel
// constructs. It answers questions like:
//   - after N seconds how many outstanding <x> things are allocated?
//   - up to this point has <Y> ever happened?
//
// Currently the only query interface to the counters is the kcounter command.
// Issue 'kcounter --help' to learn what it can do.
//
// Kernel counters public API:
//
// 1- Define a new counter in a .cc file. Do not define a counter in a header
// file as that may lead to the creation of multiple, unrelated counters. Do not
// define multiple counters with the same name.
//
//      KCOUNTER(counter_name, "<counter name>");
//
// 2- Counters start at zero. Increment the counter:
//
//      kcounter_add(counter_name, 1);
//
// By default with KCOUNTER, the `kcounter` presentation will calculate a
// sum() across cores rather than summing.
//
// Naming the counters
// The naming convention is "subsystem.thing_or_action"
// for example "dispatcher.destroy"
//             "exceptions.fpu"
//             "handles.live"
//
// Reading the counter values in code:
// Don't. The counters are maintained in a per-cpu arena and atomic
// operations are never used to set their value so they are both
// imprecise and reflect only the operations on a particular core.

class CounterDesc {
 public:
  constexpr const counters::Descriptor* begin() const { return begin_; }
  constexpr const counters::Descriptor* end() const { return end_; }
  size_t size() const { return end() - begin(); }

  constexpr auto VmoData() const { return &vmo_begin_; }

  size_t VmoDataSize() const {
    return (reinterpret_cast<uintptr_t>(vmo_end_) - reinterpret_cast<uintptr_t>(&vmo_begin_));
  }

  size_t VmoContentSize() const {
    return (reinterpret_cast<uintptr_t>(end_) - reinterpret_cast<uintptr_t>(&vmo_begin_));
  }

 private:
  // Via magic in kernel.ld, all the descriptors wind up in a contiguous
  // array bounded by these two symbols, sorted by name.
  static const counters::Descriptor begin_[] __asm__("kcountdesc_begin");
  static const counters::Descriptor end_[] __asm__("kcountdesc_end");

  // That array sits inside a region that's page-aligned and padded out to
  // page size.  The region as a whole has the DescriptorVmo layout.
  static const counters::DescriptorVmo vmo_begin_ __asm__("k_counter_desc_vmo_begin");
  static const counters::Descriptor vmo_end_[] __asm__("k_counter_desc_vmo_end");
};

class CounterArena {
 public:
  int64_t* CpuData(size_t idx) const { return &arena_[idx * CounterDesc().size()]; }

  constexpr int64_t* VmoData() const { return arena_; }

  size_t VmoDataSize() const {
    return (reinterpret_cast<uintptr_t>(arena_page_end_) - reinterpret_cast<uintptr_t>(arena_));
  }

  size_t VmoContentSize() const {
    return (reinterpret_cast<uintptr_t>(arena_end_) - reinterpret_cast<uintptr_t>(arena_));
  }

 private:
  // Parallel magic in kernel.ld allocates int64_t[SMP_MAX_CPUS] worth
  // of data space for each counter.
  static int64_t arena_[] __asm__("kcounters_arena");
  static int64_t arena_end_[] __asm__("kcounters_arena_end");
  // That's page-aligned and padded out to page size.
  static int64_t arena_page_end_[] __asm__("kcounters_arena_page_end");
};

class Counter {
 public:
  explicit constexpr Counter(const counters::Descriptor* desc) : desc_(desc) {}

  int64_t Value() const { return *Slot(); }

  void Add(int64_t delta) const {
#if defined(__aarch64__)
    // Use a relaxed atomic load/store for arm64 to avoid a potentially
    // nasty race between the regular load/store operations for a +1.
    // Relaxed atomic load/stores are about as efficient as a regular
    // load/store.
    atomic_add_64_relaxed(Slot(), delta);
#else
    // x86 can do the add in a single non atomic instruction, so the data
    // loss of a preemption in the middle of this sequence is fairly
    // minimal.
    *Slot() += delta;
#endif
  }

  // Set value of counter to |value|. No memory order is implied.
  void Set(uint64_t value) const {
#if defined(__aarch64__)
    atomic_store_64_relaxed(Slot(), value);
#else
    *Slot() = value;
#endif
  }

 protected:
  int64_t* Slot() const { return &get_local_percpu()->counters[Index()]; }

 private:
  // The order of the descriptors is the order of the slots in each per-CPU
  // array.
  constexpr size_t Index() const { return desc_ - CounterDesc().begin(); }

  const counters::Descriptor* desc_;
};

// Define the descriptor and reserve the arena space for the counters.  Place
// each kcounter_arena_* array in a .bss.kcounter.* section; kernel.ld
// recognizes those names and places them all together to become the contiguous
// kcounters_arena array.  Note that each kcounter_arena_* does not correspond
// with the slots used for this particular counter (that would have terrible
// cache effects); it just reserves enough space for counters_init() to dole
// out in per-CPU chunks.
#define KCOUNTER_DECLARE(var, name, type)                                                     \
  namespace {                                                                                 \
  static_assert(__INCLUDE_LEVEL__ == 0,                                                       \
                "kcounters should not be defined in a header as doing so may result in the "  \
                "creation of multiple unrelated counters with the same name");                \
  int64_t kcounter_arena_##var SPECIAL_SECTION(".bss.kcounter." name, int64_t)[SMP_MAX_CPUS]; \
  const counters::Descriptor kcounter_desc_##var SPECIAL_SECTION("kcountdesc." name,          \
                                                                 counters::Descriptor) = {    \
      name,                                                                                   \
      counters::Type::k##type,                                                                \
  };                                                                                          \
  constexpr Counter var(&kcounter_desc_##var);                                                \
  }  // anonymous namespace

#define KCOUNTER(var, name) KCOUNTER_DECLARE(var, name, Sum)

static inline void kcounter_add(const Counter& counter, int64_t delta) { counter.Add(delta); }

#endif  // ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTERS_H_
