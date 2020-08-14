// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_DISTANCE_MAP_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_DISTANCE_MAP_H_

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/lazy_init/lazy_init.h>
#include <stddef.h>
#include <stdint.h>

#include <fbl/alloc_checker.h>
#include <kernel/cpu.h>
#include <ktl/algorithm.h>
#include <ktl/forward.h>
#include <ktl/limits.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/unique_ptr.h>

// A compact distance matrix storing the metric distance between CPUs.
class CpuDistanceMap {
  static auto& GetStorage() {
    static lazy_init::LazyInit<CpuDistanceMap> distance_map;
    return distance_map;
  }

 public:
  ~CpuDistanceMap() = default;

  CpuDistanceMap(const CpuDistanceMap&) = delete;
  CpuDistanceMap& operator=(const CpuDistanceMap&) = delete;
  CpuDistanceMap(CpuDistanceMap&&) = default;
  CpuDistanceMap& operator=(CpuDistanceMap&&) = default;

  // Index pair that sorts the index elements so that i < j.
  struct Index {
    Index(cpu_num_t i, cpu_num_t j) : i{ktl::min(i, j)}, j{ktl::max(i, j)} {}
    const cpu_num_t i;
    const cpu_num_t j;
  };

  // The value type for metric distances.
  using Distance = uint32_t;

  // Returns the distace for the given index pair (i, j).
  Distance operator[](Index index) const {
    if (index.i == index.j) {
      return 0;
    }
    return entries_[LinearIndex(index, cpu_count_)].distance;
  }

  size_t cpu_count() const { return cpu_count_; }
  size_t entry_count() const { return entry_count_; }
  Distance distance_threshold() const { return distance_threshold_; }

  // Sets the metric distance representing the first significant distance in the
  // map. The value is not used directly by this class. Instead, it is provided
  // as a convenience for reference by consumers when processing map values.
  //
  // For example, this value may be used to communicate the threshold for auto
  // clustering from the producer of the map to the clustering logic.
  void set_distance_threshold(Distance distance_threshold) {
    distance_threshold_ = distance_threshold;
  }

  void Dump() {
    dprintf(INFO, "CPU distance map:\n");
    const auto& map = *this;
    for (cpu_num_t i = 0; i < cpu_count_; i++) {
      dprintf(INFO, "CPU %2" PRIu32 ": ", i);
      for (cpu_num_t j = 0; j < cpu_count_; j++) {
        dprintf(INFO, "%02" PRIu32 "%s", map[{i, j}], (j < cpu_count_ - 1 ? ":" : ""));
      }
      dprintf(INFO, "\n");
    }
  }

  static CpuDistanceMap& Get() { return GetStorage().Get(); }

  template <typename Callable>
  static void Initialize(size_t cpu_count, Callable&& callable) {
    if (auto result = Create(cpu_count, ktl::forward<Callable>(callable))) {
      GetStorage().Initialize(ktl::move(*result));
    } else {
      dprintf(CRITICAL, "Failed to create distance map!\n");
    }
  }

 private:
  friend struct CpuDistanceMapTestAccess;

  struct Entry {
    Distance distance;
  };

  static size_t EntryCountFromCpuCount(size_t cpu_count) {
    // Avoid silent integer overflow.
    ASSERT(cpu_count <= (size_t{1} << 32));
    return (cpu_count * cpu_count - cpu_count) / 2;
  }

  CpuDistanceMap(size_t cpu_count, size_t entry_count, ktl::unique_ptr<Entry[]> entries)
      : cpu_count_{cpu_count}, entry_count_{entry_count}, entries_{ktl::move(entries)} {}

  // Creates a distance map with the given number of entries. Invokes the given
  // callable with each unique pair of CPUs (i, j), excluding i==j, to compute
  // the distance between each pair.
  template <typename Callable>
  static ktl::optional<CpuDistanceMap> Create(size_t cpu_count, Callable&& callable) {
    if (cpu_count == 0) {
      return ktl::nullopt;
    }

    const auto entry_count = EntryCountFromCpuCount(cpu_count);
    if (entry_count == 0u) {
      return CpuDistanceMap(cpu_count, entry_count, nullptr);
    }

    if (auto distance_map = AllocateEntries(entry_count)) {
      dprintf(INFO, "Allocated %zu entries for CPU distance map.\n", entry_count);

      // Fill the distance map entries with CPU distances.
      for (cpu_num_t i = 0; i < cpu_count; i++) {
        for (cpu_num_t j = i + 1; j < cpu_count; j++) {
          if (i != j) {
            const auto linear_index = LinearIndex({i, j}, cpu_count);
            DEBUG_ASSERT(linear_index < entry_count);

            const Entry entry{static_cast<Distance>(ktl::forward<Callable>(callable)(i, j))};
            distance_map[linear_index] = entry;
          }
        }
      }

      return CpuDistanceMap{cpu_count, entry_count, ktl::move(distance_map)};
    }
    return ktl::nullopt;
  }

  // Creates a default distance map where every CPU is equidistant.
  static ktl::optional<CpuDistanceMap> Create(size_t cpu_count) {
    return Create(cpu_count, [](cpu_num_t i, cpu_num_t j) -> Distance { return i == j ? 0 : 1; });
  }

  // Returns a linear index into the compact distance matrix.
  //
  // The compact distance matrix is the upper triangle of the full distance
  // matrix, arranged in a compacted row-major linear array. Is it unnecessary
  // to store the lower triangle or the diagonal, as the full distance matrix
  // is both symmetric around the diagonal and hollow (diagonal is zero).
  //
  // This function maps the row-by-column indices (i, j) to the linear index k.
  // The mapping is defined for 0 <= i < j < n, undefined otherwise.
  //
  // Example of a full 5x5 distance matrix and the linearized upper triangle,
  // with corresponding entries of the upper, lower, and linearized triangles
  // labeled a-j:
  //
  //           0 a b c d
  //           a 0 e f g
  //           b e 0 h i    ->    [a b c d e f g h i j]
  //           c f h 0 j
  //           d g i j 0
  //
  // The mapping (i, k) -> k is derived from the following terms:
  //
  // The order of the square distance matrix:              n
  // The row-major linear offset:                          S = n*i + j
  // The triangular number of index i:                     T = (i*i + i)/2
  // The number of diagonal zeros up to row i, inclusive:  D = i + 1
  //
  // k(i, j, n) = S - T - D
  //
  // The mapping computes the row-major linear offset of (i, j) and subtracts
  // the offsets of the lower triangle and diagonal entries up to row i.
  //
  static size_t LinearIndex(Index index, size_t cpu_count) {
    DEBUG_ASSERT_MSG(index.i < cpu_count && index.j < cpu_count && index.i < index.j,
                     "i=%" PRIu32 " j=%" PRIu32 " count=%zu", index.i, index.j, cpu_count);

    const size_t square = cpu_count * index.i + index.j;
    const size_t triangle = (index.i * index.i + index.i) / 2;
    const size_t diagonal = index.i + 1;

    return square - triangle - diagonal;
  }

  static ktl::unique_ptr<Entry[]> AllocateEntries(size_t entry_count) {
    fbl::AllocChecker alloc_checker;
    ktl::unique_ptr<Entry[]> distance_map{new (&alloc_checker) Entry[entry_count]};
    if (alloc_checker.check()) {
      return distance_map;
    }
    return nullptr;
  }

  size_t cpu_count_{0};
  size_t entry_count_{0};
  Distance distance_threshold_{ktl::numeric_limits<Distance>::max()};
  ktl::unique_ptr<Entry[]> entries_{};
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_DISTANCE_MAP_H_
