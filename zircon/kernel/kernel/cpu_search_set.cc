// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/cpu_search_set.h"

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <stddef.h>

#include <fbl/alloc_checker.h>
#include <kernel/cpu_distance_map.h>
#include <ktl/algorithm.h>
#include <ktl/span.h>
#include <ktl/unique_ptr.h>

CpuSearchSet::ClusterSet CpuSearchSet::cluster_set_;

namespace {

// Resizes the given vector with default values.
// TODO(eieio): Implement fbl::Vector::resize().
template <typename T>
void Resize(fbl::Vector<T>* vector, size_t size) {
  fbl::AllocChecker checker;
  vector->reserve(size, &checker);
  ASSERT(checker.check());
  for (size_t i = 0; i < size; i++) {
    vector->push_back({}, &checker);
    ASSERT(checker.check());
  }
}

// Utility type compute CPU clusters using a disjoint set structure.
class ClusterMap {
 public:
  ClusterMap() = default;

  ClusterMap(const ClusterMap&) = delete;
  ClusterMap& operator=(const ClusterMap&) = delete;
  ClusterMap(ClusterMap&&) = default;
  ClusterMap& operator=(ClusterMap&&) = default;

  explicit operator bool() const { return !elements_.is_empty(); }

  // Creates a ClusterMap with the given number of CPUs, with each CPU initially
  // in its own cluster.
  static ClusterMap Create(size_t element_count) {
    fbl::Vector<cpu_num_t> elements;
    Resize(&elements, element_count);

    for (cpu_num_t i = 0; i < element_count; i++) {
      elements[i] = i;
    }

    return ClusterMap{ktl::move(elements)};
  }

  // Returns an iterator over the elements of the disjoint set structure.
  auto iterator() { return ktl::span{elements_.begin(), elements_.size()}; }
  auto begin() { return iterator().begin(); }
  auto end() { return iterator().end(); }

  cpu_num_t operator[](size_t index) const { return elements_[index]; }

  cpu_num_t FindSet(cpu_num_t node) {
    while (true) {
      cpu_num_t parent = elements_[node];
      cpu_num_t grandparent = elements_[parent];
      if (parent == grandparent) {
        return parent;
      }
      elements_[node] = grandparent;
      node = parent;
    }
  }

  void UnionSets(cpu_num_t a, cpu_num_t b) {
    while (true) {
      cpu_num_t root_a = FindSet(a);
      cpu_num_t root_b = FindSet(b);

      if (root_a < root_b) {
        elements_[root_b] = root_a;
      } else if (root_a > root_b) {
        elements_[root_a] = root_b;
      } else {
        return;
      }
    }
  }

  size_t ClusterCount() const {
    size_t count = 0;
    for (size_t i = 0; i < elements_.size(); i++) {
      if (elements_[i] == i) {
        count++;
      }
    }
    return count;
  }

  size_t MemberCount(cpu_num_t root) {
    size_t count = 0;
    for (cpu_num_t i = 0; i < elements_.size(); i++) {
      if (FindSet(i) == root) {
        count++;
      }
    }
    return count;
  }

 private:
  explicit ClusterMap(fbl::Vector<cpu_num_t> elements) : elements_{ktl::move(elements)} {}

  fbl::Vector<cpu_num_t> elements_{};
};

}  // anonymous namespace

CpuSearchSet::ClusterSet CpuSearchSet::DoAutoCluster(size_t cpu_count, const CpuDistanceMap& map) {
  ClusterMap cluster_map = ClusterMap::Create(cpu_count);

  // Perform a single pass of agglomerative clustering, joining CPUs with
  // distances below the significant distance threshold into the same cluster.
  for (cpu_num_t i = 0; i < cpu_count; i++) {
    for (cpu_num_t j = i + 1; j < cpu_count; j++) {
      if (map[{i, j}] < map.distance_threshold()) {
        cluster_map.UnionSets(i, j);
      }
    }
  }

  // Allocate vector of Cluster structures with an entry for each computed
  // cluster.
  fbl::AllocChecker checker;
  fbl::Vector<Cluster> clusters;
  Resize(&clusters, cluster_map.ClusterCount());

  // Alllocate a vector of MapEntry structures with an entry for each CPU.
  fbl::Vector<MapEntry> cpu_to_cluster_map;
  Resize(&cpu_to_cluster_map, cpu_count);

  // Fill in the Cluster structures and CPU-to-cluster map.
  size_t cluster_index = 0;
  for (cpu_num_t i = 0; i < cpu_count; i++) {
    const size_t member_count = cluster_map.MemberCount(i);
    if (member_count != 0) {
      Cluster& cluster = clusters[cluster_index];
      cluster.id = cluster_index;
      Resize(&cluster.members, member_count);

      size_t member_index = 0;
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (cluster_map[j] == i) {
          cluster.members[member_index] = j;
          cpu_to_cluster_map[j] = {&cluster, member_index};
          member_index++;
        }
      }
      cluster_index++;
    }
  }

  return {ktl::move(clusters), ktl::move(cpu_to_cluster_map)};
}

void CpuSearchSet::DumpClusters() {
  dprintf(INFO, "CPU clusters:\n");
  for (const Cluster& cluster : cluster_set_.iterator()) {
    dprintf(INFO, "Cluster %2zu: ", cluster.id);
    for (size_t j = 0; j < cluster.members.size(); j++) {
      dprintf(INFO, "%" PRIu32 "%s", cluster.members[j],
              j < cluster.members.size() - 1 ? ", " : "");
    }
    dprintf(INFO, "\n");
  }
}

// Initializes the search set with a unique CPU order that minimizes cache level
// crossings while attempting to maximize distribution across CPUs.
void CpuSearchSet::DoInitialize(cpu_num_t this_cpu, size_t cpu_count, const ClusterSet& cluster_set,
                                const CpuDistanceMap& map) {
  // Initialize the search set in increasing ordinal order.
  cpu_count_ = cpu_count;
  for (cpu_num_t i = 0; i < cpu_count; i++) {
    const size_t cluster = cluster_set.cpu_to_cluster_map[i].cluster->id;
    ordered_cpus_[i] = {i, cluster};
  }

  // Sort the search set by these criteria in priority order:
  //   1. Cache distance from this CPU.
  //   2. Modular cluster order, offset by this CPU's cluster id.
  //   3. Modular cluster member order, offset by this CPU's cluster member index.
  //
  // These criteria result in a relaxed Latin Square with the following
  // properties:
  //   * A CPU is always at the front of its own search list (distance is 0).
  //   * The search list is ordered by increasing cache distance.
  //   * The search order is reasonably unique compared to other CPUs (a CPU is
  //     found as few times as possible at a given offset in all search lists).
  //
  const auto comparator = [this_cpu, &cluster_set, &map](const Entry& a, const Entry& b) {
    const auto distance_a = map[{this_cpu, a.cpu}];
    const auto distance_b = map[{this_cpu, b.cpu}];
    if (distance_a != distance_b) {
      return distance_a < distance_b;
    }

    const auto [this_cluster, this_index] = cluster_set.cpu_to_cluster_map[this_cpu];
    const auto [a_cluster, a_index] = cluster_set.cpu_to_cluster_map[a.cpu];
    const auto [b_cluster, b_index] = cluster_set.cpu_to_cluster_map[b.cpu];

    const size_t cluster_count = cluster_set.clusters.size();
    const auto a_cluster_prime = (this_cluster->id + cluster_count - a_cluster->id) % cluster_count;
    const auto b_cluster_prime = (this_cluster->id + cluster_count - b_cluster->id) % cluster_count;
    if (a_cluster_prime != b_cluster_prime) {
      return a_cluster_prime < b_cluster_prime;
    }

    const auto a_count = a_cluster->members.size();
    const auto b_count = b_cluster->members.size();
    const size_t a_index_prime = a_cluster->members[(this_index + a_count - a_index) % a_count];
    const size_t b_index_prime = b_cluster->members[(this_index + b_count - b_index) % b_count];
    return a_index_prime < b_index_prime;
  };

  ktl::stable_sort(iterator().begin(), iterator().end(), comparator);
}

void CpuSearchSet::Dump() const {
  dprintf(INFO, "CPU %2" PRIu32 ": ", ordered_cpus_[0].cpu);
  for (cpu_num_t i = 0; i < cpu_count_; i++) {
    dprintf(INFO, "%2" PRIu32 "%s", ordered_cpus_[i].cpu, i < cpu_count_ - 1 ? ", " : "");
  }
  dprintf(INFO, "\n");
}
