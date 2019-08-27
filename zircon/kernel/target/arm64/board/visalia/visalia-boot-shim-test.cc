// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <ddk/platform-defs.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>
#include <zxtest/zxtest.h>

static void append_boot_item(zbi_header_t* container, uint32_t type, uint32_t extra,
                             const void* payload, uint32_t length);

#include "boot-shim-config.h"

namespace visalia_boot_shim {

zbi_header_t received_header = {};
std::unique_ptr<uint8_t[]>* received_payload = nullptr;

void AppendBootItem(zbi_header_t* container, uint32_t type, uint32_t extra, const void* payload,
                    uint32_t length) {
  ASSERT_NOT_NULL(received_payload);

  received_payload->reset(new uint8_t[length]);
  memcpy(received_payload->get(), payload, length);

  received_header.type = type;
  received_header.length = length;
  received_header.extra = extra;
}

TEST(VisaliaBootShimTest, CpuTopology) {
  std::unique_ptr<uint8_t[]> payload;
  received_payload = &payload;

  add_cpu_topology(nullptr);

  EXPECT_EQ(received_header.type, ZBI_TYPE_CPU_TOPOLOGY);
  EXPECT_EQ(received_header.extra, sizeof(zbi_topology_node_t));
  ASSERT_EQ(received_header.length, sizeof(zbi_topology_node_t) * TOPOLOGY_CPU_COUNT);
  ASSERT_TRUE(payload);

  const zbi_topology_node_t* const nodes =
      reinterpret_cast<const zbi_topology_node_t*>(payload.get());

  EXPECT_EQ(nodes[0].entity.processor.logical_ids[0], 0);
  EXPECT_EQ(nodes[1].entity.processor.logical_ids[0], 1);
  EXPECT_EQ(nodes[2].entity.processor.logical_ids[0], 2);
  EXPECT_EQ(nodes[3].entity.processor.logical_ids[0], 3);

  EXPECT_EQ(nodes[0].entity.processor.flags, ZBI_TOPOLOGY_PROCESSOR_PRIMARY);
  EXPECT_EQ(nodes[1].entity.processor.flags, 0);
  EXPECT_EQ(nodes[2].entity.processor.flags, 0);
  EXPECT_EQ(nodes[3].entity.processor.flags, 0);

  EXPECT_EQ(nodes[0].entity.processor.architecture_info.arm.cpu_id, 0);
  EXPECT_EQ(nodes[1].entity.processor.architecture_info.arm.cpu_id, 1);
  EXPECT_EQ(nodes[2].entity.processor.architecture_info.arm.cpu_id, 2);
  EXPECT_EQ(nodes[3].entity.processor.architecture_info.arm.cpu_id, 3);

  EXPECT_EQ(nodes[0].entity.processor.architecture_info.arm.gic_id, 0);
  EXPECT_EQ(nodes[1].entity.processor.architecture_info.arm.gic_id, 1);
  EXPECT_EQ(nodes[2].entity.processor.architecture_info.arm.gic_id, 2);
  EXPECT_EQ(nodes[3].entity.processor.architecture_info.arm.gic_id, 3);
}

}  // namespace visalia_boot_shim

static void append_boot_item(zbi_header_t* container, uint32_t type, uint32_t extra,
                             const void* payload, uint32_t length) {
  visalia_boot_shim::AppendBootItem(container, type, extra, payload, length);
}
