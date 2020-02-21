// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTER_VMO_ABI_H_
#define ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTER_VMO_ABI_H_

#include <inttypes.h>
#include <stddef.h>

// This file describes how the kernel exposes its internal counters to userland.
// This is a PRIVATE UNSTABLE ABI that may change at any time!  The layouts used
// here; the set of counters; their names, meanings, and types; and the set of
// available types; are all subject to change in every kernel version and are
// not meant to be any kind of stable ABI between the kernel and userland.
//
// The expectation is that these layouts will be used only by a single
// privileged service that is tightly-coupled with the kernel, i.e. always built
// from source when building the kernel.
//
// The counters exist only for kernel-specific diagnostic and logging purposes.

namespace counters {

enum class Type : uint64_t {
  kPadding = 0,
  kSum = 1,
  kMin = 2,
  kMax = 3,
};

struct Descriptor {
  char name[56];
  Type type;
};

static_assert(sizeof(Descriptor) == 64,
              "kernel.ld uses this size to ASSERT that enough space"
              " has been reserved in the counters arena");

static_assert(alignof(Descriptor) == 8,
              "kernel.ld knows there is no alignment padding between"
              " the VMO header and the descriptor table");

struct DescriptorVmo {
  // PA_VMO_KERNEL_FILE with this name has the DescriptorVmo layout.
  static constexpr char kVmoName[] = "counters/desc";

  // This is time_t as of writing.  Change it when changing this layout.
  // TODO(mcgrathr): Maybe generate these uniquely at build time from
  // the kernel version info or something?
  static constexpr uint64_t kMagic = 1547273975;

  uint64_t magic;                  // kMagic
  uint64_t max_cpus;               // SMP_MAX_CPUS
  uint64_t descriptor_table_size;  // sizeof(descriptor_table)

  constexpr size_t num_counters() const { return descriptor_table_size / sizeof(Descriptor); }

  // These are sorted by name.  This index into this table corresponds
  // to an index into a per-CPU array in the arena.
  Descriptor descriptor_table[];

  constexpr const Descriptor* begin() const { return descriptor_table; }

  constexpr const Descriptor* end() const { return &descriptor_table[num_counters()]; }
};
static_assert(offsetof(DescriptorVmo, descriptor_table_size) == 16 &&
                  offsetof(DescriptorVmo, descriptor_table) == 24,
              "kernel.ld knows the layout of DescriptorVmo");

// PA_VMO_KERNEL_FILE with this name holds an array of SMP_MAX_CPUS
// arrays, each of which is int64_t[num_counters()] index by the
// index into DescriptorVmo::descriptor_table.
static constexpr char kArenaVmoName[] = "counters/arena";

}  // namespace counters

#endif  // ZIRCON_KERNEL_LIB_COUNTERS_INCLUDE_LIB_COUNTER_VMO_ABI_H_
