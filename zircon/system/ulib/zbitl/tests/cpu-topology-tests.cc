// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>
#include <lib/zbitl/items/cpu-topology.h>
#include <lib/zbitl/storage-traits.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <string_view>

#include <gtest/gtest.h>

namespace {

class CpuConfigPayload {
 public:
  explicit CpuConfigPayload(cpp20::span<const zbi_cpu_cluster_t> clusters) {
    zbi_cpu_config_t config{
        .cluster_count = static_cast<uint32_t>(clusters.size()),
    };
    data_.append(reinterpret_cast<const std::byte*>(&config), sizeof(config));
    data_.append(reinterpret_cast<const std::byte*>(clusters.data()), clusters.size_bytes());
  }

  cpp20::span<const std::byte> as_bytes() const { return {data_}; }

 private:
  std::basic_string<std::byte> data_;
};

class CpuTopologyPayload {
 public:
  explicit CpuTopologyPayload(cpp20::span<const zbi_topology_node_t> nodes) : nodes_(nodes) {}

  cpp20::span<const std::byte> as_bytes() const { return cpp20::as_bytes(nodes_); }

 private:
  cpp20::span<const zbi_topology_node_t> nodes_;
};

void ExpectArmNodesAreEqual(const zbi_topology_node_t& expected_node,
                            const zbi_topology_node_t& actual_node) {
  ASSERT_EQ(expected_node.entity_type, actual_node.entity_type);
  EXPECT_EQ(expected_node.parent_index, actual_node.parent_index);
  switch (actual_node.entity_type) {
    case ZBI_TOPOLOGY_ENTITY_CLUSTER: {
      const zbi_topology_cluster_t& actual = actual_node.entity.cluster;
      const zbi_topology_cluster_t& expected = expected_node.entity.cluster;
      EXPECT_EQ(expected.performance_class, actual.performance_class);
      break;
    }
    case ZBI_TOPOLOGY_ENTITY_PROCESSOR: {
      const zbi_topology_processor_t& actual = actual_node.entity.processor;
      const zbi_topology_processor_t& expected = expected_node.entity.processor;
      ASSERT_EQ(expected.logical_id_count, actual.logical_id_count);
      for (size_t j = 0; j < actual.logical_id_count; ++j) {
        EXPECT_EQ(expected.logical_ids[j], actual.logical_ids[j]) << "logical_ids[" << j << "])";
      }
      EXPECT_EQ(actual.flags, expected.flags);
      ASSERT_EQ(actual.architecture, ZBI_TOPOLOGY_ARCH_ARM);
      ASSERT_EQ(expected.architecture, ZBI_TOPOLOGY_ARCH_ARM);

      const zbi_topology_arm_info_t& actual_info = actual.architecture_info.arm;
      const zbi_topology_arm_info_t& expected_info = expected.architecture_info.arm;
      EXPECT_EQ(expected_info.cluster_1_id, actual_info.cluster_1_id);
      EXPECT_EQ(expected_info.cluster_2_id, actual_info.cluster_2_id);
      EXPECT_EQ(expected_info.cluster_3_id, actual_info.cluster_3_id);
      EXPECT_EQ(expected_info.cpu_id, actual_info.cpu_id);
      EXPECT_EQ(expected_info.gic_id, actual_info.gic_id);
      break;
    }
  }
}

void ExpectTableHasArmNodes(const zbitl::CpuTopologyTable& table,
                            cpp20::span<const zbi_topology_node_t> nodes) {
  EXPECT_EQ(table.size(), nodes.size());
  EXPECT_EQ(table.size(), static_cast<size_t>(std::distance(table.begin(), table.end())));
  auto it = table.begin();
  for (size_t i = 0; i < nodes.size(); ++i, ++it) {
    const zbi_topology_node_t& expected = nodes[i];
    const zbi_topology_node_t actual = *it;
    ASSERT_NO_FATAL_FAILURE(ExpectArmNodesAreEqual(expected, actual)) << i;
  }
}

TEST(CpuTopologyTableTests, BadType) {
  CpuConfigPayload payload({});
  auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_DISCARD, payload.as_bytes());
  ASSERT_TRUE(result.is_error());
  std::string_view error = std::move(result).error_value();
  EXPECT_EQ("invalid ZBI item type for CpuTopologyTable", error);
}

TEST(CpuTopologyTableTests, NoCores) {
  // CONFIG: empty payload.
  {
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, {});
    ASSERT_TRUE(result.is_error());
    std::string_view error = std::move(result).error_value();
    EXPECT_EQ("ZBI_TYPE_CPU_CONFIG too small for header", error);
  }

  // CONFIG: empty cluster.
  {
    CpuConfigPayload payload({});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {}));
  }

  // TOPOLOGY: empty payload.
  {
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, {});
    ASSERT_TRUE(result.is_error());
    std::string_view error = std::move(result).error_value();
    EXPECT_EQ("ZBI_TYPE_CPU_TOPOLOGY payload is empty", error);
  }
}

TEST(CpuTopologyTableTests, SingleArmCore) {
  constexpr zbi_cpu_cluster_t kConfig[] = {{.cpu_count = 1}};
  constexpr zbi_topology_node_t kNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
  };

  {
    CpuConfigPayload payload({kConfig});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
  {
    CpuTopologyPayload payload({kNodes});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
}

TEST(CpuTopologyTableTests, TwoArmCoresAcrossOneCluster) {
  constexpr zbi_cpu_cluster_t kConfig[] = {{.cpu_count = 2}};
  constexpr zbi_topology_node_t kNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 1,
                                          .gic_id = 1,
                                      },
                              },
                      },
              },
      },
  };

  {
    CpuConfigPayload payload({kConfig});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
  {
    CpuTopologyPayload payload({kNodes});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
}

TEST(CpuTopologyTableTests, FourArmCoresAcrossOneCluster) {
  constexpr zbi_cpu_cluster_t kConfig[] = {{.cpu_count = 4}};
  constexpr zbi_topology_node_t kNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 1,
                                          .gic_id = 1,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {2},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 2,
                                          .gic_id = 2,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {3},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 3,
                                          .gic_id = 3,
                                      },
                              },
                      },
              },
      },
  };

  {
    CpuConfigPayload payload({kConfig});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
  {
    CpuTopologyPayload payload({kNodes});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
}

TEST(CpuTopologyTableTests, TwoArmCoresAcrossTwoClusters) {
  constexpr zbi_cpu_cluster_t kConfig[] = {{.cpu_count = 1}, {.cpu_count = 1}};
  constexpr zbi_topology_node_t kNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 1}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 2,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 0,
                                          .gic_id = 1,
                                      },
                              },
                      },
              },
      },
  };

  {
    CpuConfigPayload payload({kConfig});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
  {
    CpuTopologyPayload payload({kNodes});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
}

TEST(CpuTopologyTableTests, SixArmCoresAcrossThreeClusters) {
  constexpr zbi_cpu_cluster_t kConfig[] = {{.cpu_count = 1}, {.cpu_count = 3}, {.cpu_count = 2}};
  constexpr zbi_topology_node_t kNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 1}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 2,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 0,
                                          .gic_id = 1,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 2,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {2},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 1,
                                          .gic_id = 2,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 2,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {3},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 2,
                                          .gic_id = 3,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = ZBI_TOPOLOGY_NO_PARENT,
          .entity = {.cluster = {.performance_class = 2}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 6,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {4},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 2,
                                          .cpu_id = 0,
                                          .gic_id = 4,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 6,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {5},
                          .logical_id_count = 1,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 2,
                                          .cpu_id = 1,
                                          .gic_id = 5,
                                      },
                              },
                      },
              },
      },
  };

  {
    CpuConfigPayload payload({kConfig});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_CONFIG, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
  {
    CpuTopologyPayload payload({kNodes});
    auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
    ASSERT_FALSE(result.is_error()) << result.error_value();
    const auto table = std::move(result).value();
    ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kNodes}));
  }
}

TEST(CpuTopologyTableTests, Sherlock) {
  // The CPU topology of the Sherlock board.
  constexpr zbi_topology_node_t kSherlockNodes[] = {
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = 0,
          .entity = {.cluster = {.performance_class = 0}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {0},
                          .logical_id_count = 1,
                          .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 0,
                                          .gic_id = 0,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 0,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {1},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 0,
                                          .cpu_id = 1,
                                          .gic_id = 1,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
          .parent_index = 0,
          .entity = {.cluster = {.performance_class = 1}},
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 3,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {2},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 0,
                                          .gic_id = 4,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 3,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {3},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 1,
                                          .gic_id = 5,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 3,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {4},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 2,
                                          .gic_id = 6,
                                      },
                              },
                      },
              },
      },
      {
          .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
          .parent_index = 3,
          .entity =
              {
                  .processor =
                      {
                          .logical_ids = {5},
                          .logical_id_count = 1,
                          .flags = 0,
                          .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                          .architecture_info =
                              {
                                  .arm =
                                      {
                                          .cluster_1_id = 1,
                                          .cpu_id = 3,
                                          .gic_id = 7,
                                      },
                              },
                      },
              },
      },
  };

  CpuTopologyPayload payload({kSherlockNodes});
  auto result = zbitl::CpuTopologyTable::FromPayload(ZBI_TYPE_CPU_TOPOLOGY, payload.as_bytes());
  ASSERT_FALSE(result.is_error()) << result.error_value();
  const auto table = std::move(result).value();
  ASSERT_NO_FATAL_FAILURE(ExpectTableHasArmNodes(table, {kSherlockNodes}));
}

}  // namespace
