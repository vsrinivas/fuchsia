// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_SEARCH_SET_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_SEARCH_SET_H_

#include <stddef.h>

#include <fbl/vector.h>
#include <kernel/cpu.h>
#include <kernel/cpu_distance_map.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/span.h>
#include <ktl/unique_ptr.h>

// CpuSearchSet is a cache/cluster-aware search list that minimizes cache
// crossings and maximizes remote CPU access distribution when searching for a
// target CPU to place a task.
class CpuSearchSet {
 public:
  CpuSearchSet() = default;

  CpuSearchSet(const CpuSearchSet&) = default;
  CpuSearchSet& operator=(const CpuSearchSet&) = default;

  // Entry type for the list of CPUs.
  struct Entry {
    cpu_num_t cpu;
    size_t cluster;
  };

  // Returns an iterator over the CPU search list. Forward iteration produces
  // entries in order of decreasing preference (i.e. earlier entries are more
  // optimal).
  auto const_iterator() const { return ktl::span{ordered_cpus_.cbegin(), cpu_count_}; }

  // Dumps the CPU search list for this set to the debug log.
  void Dump() const;

  // Dumps the CPU clusters to the debug log.
  static void DumpClusters();

  size_t cpu_count() const { return cpu_count_; }

  size_t cluster() const { return ordered_cpus_[0].cluster; }

 private:
  friend struct percpu;
  friend struct CpuSearchSetTestAccess;

  // Private non-const CPU search list and cluster iterators.
  auto iterator() { return ktl::span{ordered_cpus_.begin(), cpu_count_}; }

  // Called once at percpu secondary init to compute the logical clusters from
  // the topology-derived distance map.
  static void AutoCluster(size_t cpu_count) {
    cluster_set_ = DoAutoCluster(cpu_count, CpuDistanceMap::Get());
  }

  // Forward declaration.
  struct ClusterSet;

  // Testable body of AutoCluster().
  static ClusterSet DoAutoCluster(size_t cpu_count, const CpuDistanceMap& map);

  // Called once per CPU at percpu secondary init to compute the unique, cache-
  // aware CPU search order for the CPUs.
  void Initialize(cpu_num_t this_cpu, size_t cpu_count) {
    DoInitialize(this_cpu, cpu_count, cluster_set_, CpuDistanceMap::Get());
  }

  // Testable body of Initialize().
  void DoInitialize(cpu_num_t this_cpu, size_t cpu_count, const ClusterSet& cluster_set,
                    const CpuDistanceMap& map);

  // Each search set is initially populated by CPU 0 so that the boot processor
  // has a valid search set during early kernel init.
  // TODO(eieio): This depends on the assumption that the boot processor is
  // always logical CPU id 0. This assumption exists in other places and may
  // need to be addressed in the future.
  size_t cpu_count_{1};
  ktl::array<Entry, SMP_MAX_CPUS> ordered_cpus_{{Entry{0, 0}}};

  // Type representing a logical CPU cluster and its members.
  struct Cluster {
    size_t id{0};
    fbl::Vector<cpu_num_t> members{};
  };

  // Entry type for the logical CPU id to cluster map.
  struct MapEntry {
    // The cluster the logical CPU id belongs to.
    Cluster* cluster;

    // The index of the logical CPU in the Cluster::members list.
    size_t index;
  };

  // Represents a set of logical CPU clusters.
  struct ClusterSet {
    // The list of logical clusters computed by auto-clustering.
    fbl::Vector<Cluster> clusters{};

    // Map from logical CPU id to logical cluster.
    fbl::Vector<MapEntry> cpu_to_cluster_map{};

    auto iterator() { return ktl::span{clusters.begin(), clusters.size()}; }
  };

  // The global set of CPU clusters initialized during auto clustering.
  static ClusterSet cluster_set_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_CPU_SEARCH_SET_H_
