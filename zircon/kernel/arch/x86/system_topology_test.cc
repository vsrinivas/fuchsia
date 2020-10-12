// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/system_topology.h"

#include <lib/acpi_tables_test_data.h>
#include <lib/system-topology.h>
#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

#include <arch/x86/cpuid_test_data.h>

using system_topology::Graph;

namespace {

// Uses test data from a HP z840, dual socket Xeon E2690v4.
bool test_cpus_z840() {
  BEGIN_TEST;

  fbl::Vector<zbi_topology_node_t> flat_topology;
  auto status = x86::GenerateFlatTopology(cpu_id::kCpuIdXeon2690v4,
                                          acpi_test_data::kZ840TableProvider, &flat_topology);
  ASSERT_EQ(ZX_OK, status);

  int numa_count = 0;
  int die_count = 0;
  int cache_count = 0;
  int core_count = 0;
  int thread_count = 0;
  int last_numa = -1;
  int last_die = -1;
  int last_cache = -1;
  for (int i = 0; i < (int)flat_topology.size(); i++) {
    const auto& node = flat_topology[i];
    switch (node.entity_type) {
      case ZBI_TOPOLOGY_ENTITY_NUMA_REGION:
        last_numa = i;
        numa_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_DIE:
        EXPECT_EQ(last_numa, node.parent_index);
        last_die = i;
        die_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_CACHE:
        EXPECT_EQ(last_die, node.parent_index);
        last_cache = i;
        cache_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_PROCESSOR:
        EXPECT_EQ(last_cache, node.parent_index);
        core_count++;
        thread_count += node.entity.processor.logical_id_count;
        break;
    }
  }

  // We expect two numa regions, two dies, two caches, and 28 cores.
  EXPECT_EQ(2u + 2u + 2u + 28u, flat_topology.size());
  EXPECT_EQ(2, numa_count);
  EXPECT_EQ(2, die_count);
  EXPECT_EQ(2, cache_count);
  EXPECT_EQ(28, core_count);
  EXPECT_EQ(56, thread_count);

  // Ensure the format can be parsed and validated by the system topology library.
  Graph graph;
  EXPECT_EQ(ZX_OK, Graph::Initialize(&graph, flat_topology.data(), flat_topology.size()));

  END_TEST;
}

// Uses test data from a Threadripper 2970wx system with x399 chipset.
bool test_cpus_2970wx_x399() {
  BEGIN_TEST;

  fbl::Vector<zbi_topology_node_t> flat_topology;
  auto status = x86::GenerateFlatTopology(cpu_id::kCpuIdThreadRipper2970wx,
                                          acpi_test_data::k2970wxTableProvider, &flat_topology);
  ASSERT_EQ(ZX_OK, status);

  int numa_count = 0;
  int die_count = 0;
  int core_count = 0;
  int cache_count = 0;
  int thread_count = 0;
  int last_numa = -1;
  int last_die = -1;
  int last_cache = -1;
  for (int i = 0; i < (int)flat_topology.size(); i++) {
    const auto& node = flat_topology[i];
    switch (node.entity_type) {
      case ZBI_TOPOLOGY_ENTITY_NUMA_REGION:
        last_numa = i;
        numa_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_DIE:
        EXPECT_EQ(last_numa, node.parent_index);
        last_die = i;
        die_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_CACHE:
        EXPECT_EQ(last_die, node.parent_index);
        last_cache = i;
        cache_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_PROCESSOR:
        EXPECT_EQ(last_cache, node.parent_index);
        core_count++;
        thread_count += node.entity.processor.logical_id_count;
        break;
    }
  }

  EXPECT_EQ(4, numa_count);
  EXPECT_EQ(4, die_count);
  EXPECT_EQ(8, cache_count);
  EXPECT_EQ(24, core_count);
  EXPECT_EQ(48, thread_count);

  // Ensure the format can be parsed and validated by the system topology library.
  Graph graph;
  EXPECT_EQ(ZX_OK, Graph::Initialize(&graph, flat_topology.data(), flat_topology.size()));

  END_TEST;
}

// Uses bad data to trigger the fallback topology, (everything flat).
bool test_cpus_fallback() {
  BEGIN_TEST;

  // The minimal cpuid data set to fallthrough our level parsing.
  // The number of processors is pulled from acpi so combining these gets us a
  // bad-cpuid but good acpi condition we are seeing on some hypervisors.
  const cpu_id::TestDataSet fallback_cpuid_data = {
      .features = {},
      .missing_features = {},
      .leaf0 = {},
      .leaf1 = {},
      .leaf4 = {},
      .leaf6 = {},
      .leaf7 = {},
      .leafB = {},
      .leaf8_0 = {.reg = {0x80000008, 0x0, 0x0}},
      .leaf8_1 = {},
      .leaf8_7 = {},
      .leaf8_8 = {},
      .leaf8_1D = {},
      .leaf8_1E = {},
  };

  fbl::Vector<zbi_topology_node_t> flat_topology;
  auto status = x86::GenerateFlatTopology(cpu_id::FakeCpuId(fallback_cpuid_data),
                                          acpi_test_data::kEveTableProvider, &flat_topology);
  ASSERT_EQ(ZX_OK, status);

  int numa_count = 0;
  int die_count = 0;
  int core_count = 0;
  int cache_count = 0;
  int thread_count = 0;
  for (int i = 0; i < (int)flat_topology.size(); i++) {
    const auto& node = flat_topology[i];
    switch (node.entity_type) {
      case ZBI_TOPOLOGY_ENTITY_NUMA_REGION:
        numa_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_DIE:
        die_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_CACHE:
        cache_count++;
        break;
      case ZBI_TOPOLOGY_ENTITY_PROCESSOR:
        core_count++;
        thread_count += node.entity.processor.logical_id_count;
        break;
    }
  }

  EXPECT_EQ(0, numa_count);
  EXPECT_EQ(1, die_count);
  EXPECT_EQ(0, cache_count);
  EXPECT_EQ(4, core_count);
  EXPECT_EQ(4, thread_count);

  // Ensure the format can be parsed and validated by the system topology library.
  Graph graph;
  EXPECT_EQ(ZX_OK, Graph::Initialize(&graph, flat_topology.data(), flat_topology.size()));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(x86_topology_tests)
UNITTEST("Enumerate cpus using data from HP z840.", test_cpus_z840)
UNITTEST("Enumerate cpus using data from ThreadRipper 2970wx/X399.", test_cpus_2970wx_x399)
UNITTEST("Enumerate cpus using data triggering fallback.", test_cpus_fallback)
UNITTEST_END_TESTCASE(x86_topology_tests, "x86_topology",
                      "Test determining x86 topology through CPUID/APIC.")
