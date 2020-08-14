// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <stddef.h>

#include <kernel/cpu.h>
#include <kernel/cpu_distance_map.h>
#include <kernel/cpu_search_set.h>
#include <ktl/algorithm.h>
#include <ktl/limits.h>
#include <ktl/optional.h>

namespace {

// Returns true if first count bits in the CPU mask are set.
bool CpuSetCheck(cpu_mask_t mask, size_t count) {
  size_t set = 0;
  for (cpu_num_t i = 0; i < count; i++) {
    if (mask & cpu_num_to_mask(i)) {
      set++;
    }
  }
  return set == count;
}

}  // anonymous namespace

// Define test types with friend access to CpuDistanceMap and CpuSearchSet.
struct CpuDistanceMapTestAccess {
  template <typename Callable>
  static ktl::optional<CpuDistanceMap> Create(size_t cpu_count, Callable&& callable) {
    return CpuDistanceMap::Create(cpu_count, std::forward<Callable>(callable));
  }
  static size_t EntryCountFromCpuCount(size_t cpu_count) {
    return CpuDistanceMap::EntryCountFromCpuCount(cpu_count);
  }
  static size_t LinearIndex(CpuDistanceMap::Index index, size_t cpu_count) {
    return CpuDistanceMap::LinearIndex(index, cpu_count);
  }
};

struct CpuSearchSetTestAccess {
  static CpuSearchSet::ClusterSet DoAutoCluster(size_t cpu_count, const CpuDistanceMap& map) {
    return CpuSearchSet::DoAutoCluster(cpu_count, map);
  }
  static void DoInitialize(CpuSearchSet* set, cpu_num_t this_cpu, size_t cpu_count,
                           const CpuSearchSet::ClusterSet& cluster_set, const CpuDistanceMap& map) {
    set->DoInitialize(this_cpu, cpu_count, cluster_set, map);
  }
};

static bool distance_map_linear_index_test() {
  BEGIN_TEST;

  // Test that the linear index function does not underflow over a large set of
  // values. The function is mathematically proven to never underflow, but we
  // test anyway in case someone changes the function.
  const size_t cpu_count = 8192;
  const size_t entry_count = CpuDistanceMapTestAccess::EntryCountFromCpuCount(cpu_count);
  for (cpu_num_t i = 0; i < cpu_count; i++) {
    for (cpu_num_t j = i + 1; j < cpu_count; j++) {
      if (i != j) {
        EXPECT_GT(entry_count, CpuDistanceMapTestAccess::LinearIndex({i, j}, cpu_count));
      }
    }
  }

  END_TEST;
}

static bool allocate_distance_map_tests() {
  BEGIN_TEST;

  {
    const size_t cpu_count = 0u;
    size_t invocations = 0;
    auto map = CpuDistanceMapTestAccess::Create(cpu_count, [&invocations](cpu_num_t, cpu_num_t) {
      invocations++;
      return 1u;
    });
    EXPECT_FALSE(map.has_value());
    EXPECT_EQ(0u, invocations);
  }

  {
    const size_t cpu_count = 1u;
    const size_t entry_count = CpuDistanceMapTestAccess::EntryCountFromCpuCount(cpu_count);
    size_t invocations = 0;
    auto map = CpuDistanceMapTestAccess::Create(cpu_count, [&invocations](cpu_num_t, cpu_num_t) {
      invocations++;
      return 1u;
    });
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(cpu_count, map->cpu_count());
    EXPECT_EQ(entry_count, map->entry_count());
    EXPECT_EQ(entry_count, invocations);
  }

  {
    const size_t cpu_count = 2u;
    const size_t entry_count = CpuDistanceMapTestAccess::EntryCountFromCpuCount(cpu_count);
    size_t invocations = 0;
    auto map = CpuDistanceMapTestAccess::Create(cpu_count, [&invocations](cpu_num_t, cpu_num_t) {
      invocations++;
      return 1u;
    });
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(cpu_count, map->cpu_count());
    EXPECT_EQ(entry_count, map->entry_count());
    EXPECT_EQ(entry_count, invocations);
  }

  {
    const size_t cpu_count = 32u;
    const size_t entry_count = CpuDistanceMapTestAccess::EntryCountFromCpuCount(cpu_count);
    size_t invocations = 0;
    auto map = CpuDistanceMapTestAccess::Create(cpu_count, [&invocations](cpu_num_t, cpu_num_t) {
      invocations++;
      return 1u;
    });
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(cpu_count, map->cpu_count());
    EXPECT_EQ(entry_count, map->entry_count());
    EXPECT_EQ(entry_count, invocations);
  }

  {
    // Request a large number of CPUs without triggering the integer overflow ASSERT.
    const size_t cpu_count = size_t{1} << 32;
    size_t invocations = 0;
    auto map = CpuDistanceMapTestAccess::Create(cpu_count, [&invocations](cpu_num_t, cpu_num_t) {
      invocations++;
      return 1u;
    });
    EXPECT_FALSE(map.has_value());
    EXPECT_EQ(0u, invocations);
  }

  END_TEST;
}

static bool distance_map_entry_tests() {
  BEGIN_TEST;

  {
    const size_t cpu_count = 1u;
    auto maybe_map =
        CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
    ASSERT_TRUE(maybe_map.has_value());
    CpuDistanceMap& map = maybe_map.value();

    for (cpu_num_t i = 0; i < cpu_count; i++) {
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (i == j) {
          EXPECT_EQ(0u, (map[{i, j}]));
        } else {
          EXPECT_EQ(1u, (map[{i, j}]));
        }
      }
    }
  }

  {
    const size_t cpu_count = 2u;
    auto maybe_map =
        CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
    ASSERT_TRUE(maybe_map.has_value());
    CpuDistanceMap& map = maybe_map.value();

    for (cpu_num_t i = 0; i < cpu_count; i++) {
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (i == j) {
          EXPECT_EQ(0u, (map[{i, j}]));
        } else {
          EXPECT_EQ(1u, (map[{i, j}]));
        }
      }
    }
  }

  {
    const size_t cpu_count = 32u;
    auto maybe_map =
        CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
    ASSERT_TRUE(maybe_map.has_value());
    CpuDistanceMap& map = maybe_map.value();

    for (cpu_num_t i = 0; i < cpu_count; i++) {
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (i == j) {
          EXPECT_EQ(0u, (map[{i, j}]));
        } else {
          EXPECT_EQ(1u, (map[{i, j}]));
        }
      }
    }
  }

  {
    const size_t cpu_count = 1u;
    auto maybe_map = CpuDistanceMapTestAccess::Create(
        cpu_count, [](cpu_num_t i, cpu_num_t j) { return ktl::max(i, j); });
    ASSERT_TRUE(maybe_map.has_value());
    CpuDistanceMap& map = maybe_map.value();

    for (cpu_num_t i = 0; i < cpu_count; i++) {
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (i == j) {
          EXPECT_EQ(0u, (map[{i, j}]));
        } else {
          EXPECT_EQ(ktl::max(i, j), (map[{i, j}]));
        }
      }
    }
  }

  {
    const size_t cpu_count = 32u;
    auto maybe_map = CpuDistanceMapTestAccess::Create(
        cpu_count, [](cpu_num_t i, cpu_num_t j) { return ktl::max(i, j); });
    ASSERT_TRUE(maybe_map.has_value());
    CpuDistanceMap& map = maybe_map.value();

    for (cpu_num_t i = 0; i < cpu_count; i++) {
      for (cpu_num_t j = 0; j < cpu_count; j++) {
        if (i == j) {
          EXPECT_EQ(0u, (map[{i, j}]));
        } else {
          EXPECT_EQ(ktl::max(i, j), (map[{i, j}]));
        }
      }
    }
  }

  END_TEST;
}

static bool default_search_set_test() {
  BEGIN_TEST;

  // A default constructed search set must have one CPU that must be CPU 0.
  CpuSearchSet search_set = {};
  EXPECT_EQ(1u, search_set.cpu_count());
  auto iter = search_set.const_iterator();
  ASSERT_TRUE(iter.begin() != iter.end());
  EXPECT_EQ(0u, iter.begin()->cpu);

  END_TEST;
}

// Use static search set to avoid overflowing the kernel stack.
static CpuSearchSet search_set;

static bool cpu_search_set_test_1() {
  BEGIN_TEST;

  const size_t cpu_count = 1u;
  auto maybe_map =
      CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
  ASSERT_TRUE(maybe_map.has_value());

  const uint32_t distance_threshold = 2u;
  maybe_map.value().set_distance_threshold(distance_threshold);
  const CpuDistanceMap& map = maybe_map.value();

  const cpu_num_t cpu0 = 0u;

  // A cluster set for one CPU should contain one cluster with CPU 0 in it.
  auto cluster_set = CpuSearchSetTestAccess::DoAutoCluster(cpu_count, map);
  ASSERT_EQ(1u, cluster_set.clusters.size());
  EXPECT_EQ(0u, cluster_set.clusters[0].id);
  ASSERT_EQ(cpu_count, cluster_set.clusters[0].members.size());
  EXPECT_EQ(cpu0, cluster_set.clusters[0].members[0]);

  CpuSearchSetTestAccess::DoInitialize(&search_set, cpu0, cpu_count, cluster_set, map);
  EXPECT_EQ(cpu_count, search_set.cpu_count());

  // Check that each CPU is in the search set.
  cpu_mask_t cpu_set = 0;
  for (const auto entry : search_set.const_iterator()) {
    ASSERT_GT(cpu_count, entry.cpu);
    cpu_set |= cpu_num_to_mask(entry.cpu);
  }
  EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));

  END_TEST;
}

static bool cpu_search_set_test_2() {
  BEGIN_TEST;

  const size_t cpu_count = 2u;
  auto maybe_map =
      CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
  ASSERT_TRUE(maybe_map.has_value());

  const uint32_t distance_threshold = 2u;
  maybe_map.value().set_distance_threshold(distance_threshold);
  const CpuDistanceMap& map = maybe_map.value();

  const cpu_num_t cpu0 = 0u;
  const cpu_num_t cpu1 = 1u;

  // A cluster set for two unit distance CPUs should contain one cluster with
  // CPU 0 and 1 in it.
  auto cluster_set = CpuSearchSetTestAccess::DoAutoCluster(cpu_count, map);
  ASSERT_EQ(1u, cluster_set.clusters.size());
  EXPECT_EQ(0u, cluster_set.clusters[0].id);
  ASSERT_EQ(cpu_count, cluster_set.clusters[0].members.size());
  EXPECT_EQ(cpu0, cluster_set.clusters[0].members[0]);
  EXPECT_EQ(cpu1, cluster_set.clusters[0].members[1]);

  {
    // The search set for CPU 0 should have two entries with CPU 0 as the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu0, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu0, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  {
    // The search set for CPU 1 should have two entries with CPU 1 as the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu1, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu1, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  END_TEST;
}

static bool cpu_search_set_test_4() {
  BEGIN_TEST;

  const size_t cpu_count = 4u;
  auto maybe_map = CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t i, cpu_num_t j) {
    return (i == 0 && j == 1) || (i == 2 && j == 3) ? 1u : 2u;
  });
  ASSERT_TRUE(maybe_map.has_value());

  const uint32_t distance_threshold = 2u;
  maybe_map.value().set_distance_threshold(distance_threshold);
  const CpuDistanceMap& map = maybe_map.value();

  const cpu_num_t cpu0 = 0u;
  const cpu_num_t cpu3 = 3u;

  // A cluster set for two sets of two equidistant CPUs should contain two
  // clusters with pairs CPU 0/1 and 2/3.
  auto cluster_set = CpuSearchSetTestAccess::DoAutoCluster(cpu_count, map);
  ASSERT_EQ(2u, cluster_set.clusters.size());
  EXPECT_EQ(0u, cluster_set.clusters[0].id);
  EXPECT_EQ(1u, cluster_set.clusters[1].id);
  ASSERT_EQ(2u, cluster_set.clusters[0].members.size());
  ASSERT_EQ(2u, cluster_set.clusters[1].members.size());
  EXPECT_EQ(cpu0, cluster_set.clusters[0].members[0]);
  EXPECT_EQ(cpu3, cluster_set.clusters[1].members[1]);

  {
    // The search set for CPU 0 should have four entries with CPU 0 as the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu0, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu0, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  {
    // The search set for CPU 3 should have four entries with CPU 3 as the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu3, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu3, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  END_TEST;
}

static bool cpu_search_set_test_max() {
  BEGIN_TEST;

  const size_t cpu_count = SMP_MAX_CPUS;
  auto maybe_map =
      CpuDistanceMapTestAccess::Create(cpu_count, [](cpu_num_t, cpu_num_t) { return 1u; });
  ASSERT_TRUE(maybe_map.has_value());

  const uint32_t distance_threshold = 2u;
  maybe_map.value().set_distance_threshold(distance_threshold);
  const CpuDistanceMap& map = maybe_map.value();

  const cpu_num_t cpu0 = 0u;
  const cpu_num_t cpu_max = cpu_count - 1;

  // A cluster set for 32 equidistant CPUs should contain one cluster with
  // all 32 CPUs in it.
  auto cluster_set = CpuSearchSetTestAccess::DoAutoCluster(cpu_count, map);
  ASSERT_EQ(1u, cluster_set.clusters.size());
  EXPECT_EQ(0u, cluster_set.clusters[0].id);
  ASSERT_EQ(cpu_count, cluster_set.clusters[0].members.size());
  EXPECT_EQ(cpu0, cluster_set.clusters[0].members[0]);
  EXPECT_EQ(cpu_max, cluster_set.clusters[0].members[cpu_max]);

  {
    // The search set for CPU 0 should have max entries with CPU 0 as the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu0, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu0, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  {
    // The search set for the last CPU should have max entries with last CPU as
    // the first entry.
    CpuSearchSetTestAccess::DoInitialize(&search_set, cpu_max, cpu_count, cluster_set, map);
    EXPECT_EQ(cpu_count, search_set.cpu_count());
    EXPECT_EQ(cpu_max, search_set.const_iterator().begin()->cpu);

    // Check that each CPU is in the search set.
    cpu_mask_t cpu_set = 0;
    for (const auto entry : search_set.const_iterator()) {
      ASSERT_GT(cpu_count, entry.cpu);
      cpu_set |= cpu_num_to_mask(entry.cpu);
    }
    EXPECT_TRUE(CpuSetCheck(cpu_set, cpu_count));
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(cpu_distance_map_tests)
UNITTEST("distance_map_linear_index", distance_map_linear_index_test)
UNITTEST("allocate_distance_map", allocate_distance_map_tests)
UNITTEST("distance_map_entries", distance_map_entry_tests)
UNITTEST_END_TESTCASE(cpu_distance_map_tests, "cpu_distance_map_tests", "cpu_distance_map_tests")

UNITTEST_START_TESTCASE(cpu_search_set_tests)
UNITTEST("default_search_set_test", default_search_set_test)
UNITTEST("cpu_search_set_test_1", cpu_search_set_test_1)
UNITTEST("cpu_search_set_test_2", cpu_search_set_test_2)
UNITTEST("cpu_search_set_test_4", cpu_search_set_test_4)
UNITTEST("cpu_search_set_test_max", cpu_search_set_test_max)
UNITTEST_END_TESTCASE(cpu_search_set_tests, "cpu_search_set_tests", "cpu_search_set_tests")
