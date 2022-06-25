// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib.h"

#include <assert.h>

size_t serialize_zbi_topology_x86_info_t(uint8_t buffer[], uint32_t apic_ids[4],
                                         uint32_t apic_id_count) {
  zbi_topology_x86_info_t node = {.apic_ids = {apic_ids[0], apic_ids[1], apic_ids[2], apic_ids[3]},
                                  .apic_id_count = apic_id_count};
  size_t size = sizeof(zbi_topology_x86_info_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_arm_info_t(uint8_t buffer[], uint8_t cluster_1_id,
                                         uint8_t cluster_2_id, uint8_t cluster_3_id, uint8_t cpu_id,
                                         uint8_t gic_id) {
  zbi_topology_arm_info_t node = {.cluster_1_id = cluster_1_id,
                                  .cluster_2_id = cluster_2_id,
                                  .cluster_3_id = cluster_3_id,
                                  .cpu_id = cpu_id,
                                  .gic_id = gic_id};
  size_t size = sizeof(zbi_topology_arm_info_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_cache_t(uint8_t buffer[], uint32_t cache_id) {
  zbi_topology_cache_t node = {.cache_id = cache_id};
  size_t size = sizeof(zbi_topology_cache_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_numa_region_t(uint8_t buffer[], uint64_t start_address,
                                            uint64_t end_address) {
  zbi_topology_numa_region_t node = {.start_address = start_address, .end_address = end_address};
  size_t size = sizeof(zbi_topology_numa_region_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_cluster_t(uint8_t buffer[], uint8_t performance_class) {
  zbi_topology_cluster_t node = {.performance_class = performance_class};
  size_t size = sizeof(zbi_topology_cluster_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_processor_t(uint8_t buffer[], uint16_t logical_ids[],
                                          uint8_t logical_id_count, uint16_t flags,
                                          uint8_t architecture,
                                          architecture_info_t architecture_info) {
  zbi_topology_processor_t node = {
      .logical_ids = {logical_ids[0], logical_ids[1], logical_ids[2], logical_ids[3]}};
  node.logical_id_count = logical_id_count;
  node.flags = flags;
  node.architecture = architecture;
  if (node.architecture == zbi_topology_architecture_t::ZBI_TOPOLOGY_ARCH_ARM) {
    node.architecture_info.arm = architecture_info.arm;
  } else if (node.architecture == zbi_topology_architecture_t::ZBI_TOPOLOGY_ARCH_X86) {
    node.architecture_info.x86 = architecture_info.x86;
  } else {
    assert(node.architecture == zbi_topology_architecture_t::ZBI_TOPOLOGY_ARCH_UNDEFINED);
  }

  size_t size = sizeof(zbi_topology_processor_t);
  std::memcpy(buffer, &node, size);
  return size;
}

size_t serialize_zbi_topology_node_t(uint8_t buffer[], uint8_t entity_type, uint16_t parent_index,
                                     entity_t entity) {
  zbi_topology_node_t node = {.entity_type = entity_type, .parent_index = parent_index};

  if (node.entity_type == zbi_topology_entity_type_t::ZBI_TOPOLOGY_ENTITY_PROCESSOR) {
    node.entity.processor = entity.processor;
  } else if (node.entity_type == zbi_topology_entity_type_t::ZBI_TOPOLOGY_ENTITY_CLUSTER) {
    node.entity.cluster = entity.cluster;
  } else if (node.entity_type == zbi_topology_entity_type_t::ZBI_TOPOLOGY_ENTITY_NUMA_REGION) {
    node.entity.numa_region = entity.numa_region;
  } else if (node.entity_type == zbi_topology_entity_type_t::ZBI_TOPOLOGY_ENTITY_CACHE) {
    node.entity.cache = entity.cache;
  }

  size_t size = sizeof(zbi_topology_node_t);
  std::memcpy(buffer, &node, size);
  return size;
}
