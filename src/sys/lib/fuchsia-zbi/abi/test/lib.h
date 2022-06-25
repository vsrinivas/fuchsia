// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_LIB_FUCHSIA_ZBI_ABI_TEST_LIB_H_
#define SRC_SYS_LIB_FUCHSIA_ZBI_ABI_TEST_LIB_H_

#include <zircon/boot/image.h>

#include <cstring>

extern "C" {
using architecture_info_t = union {
  zbi_topology_arm_info_t arm;
  zbi_topology_x86_info_t x86;
};

using entity_t = union {
  zbi_topology_processor_t processor;
  zbi_topology_cluster_t cluster;
  zbi_topology_numa_region_t numa_region;
  zbi_topology_cache_t cache;
};

size_t serialize_zbi_topology_x86_info_t(uint8_t buffer[], uint32_t apic_ids[],
                                         uint32_t apic_id_count);

size_t serialize_zbi_topology_arm_info_t(uint8_t buffer[], uint8_t cluster_1_id,
                                         uint8_t cluster_2_id, uint8_t cluster_3_id, uint8_t cpu_id,
                                         uint8_t gic_id);

size_t serialize_zbi_topology_cache_t(uint8_t buffer[], uint32_t cache_id);

size_t serialize_zbi_topology_numa_region_t(uint8_t buffer[], uint64_t start_address,
                                            uint64_t end_address);

size_t serialize_zbi_topology_cluster_t(uint8_t buffer[], uint8_t performance_class);

size_t serialize_zbi_topology_processor_t(uint8_t buffer[], uint16_t logical_ids[],
                                          uint8_t logical_id_count, uint16_t flags,
                                          uint8_t architecture,
                                          architecture_info_t architecture_info);

size_t serialize_zbi_topology_node_t(uint8_t buffer[], uint8_t entity_type, uint16_t parent_index,
                                     entity_t entity);
}

#endif  // SRC_SYS_LIB_FUCHSIA_ZBI_ABI_TEST_LIB_H_
