// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

#include <gtest/gtest.h>

#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

TEST(VirtioQueueTest, VirtioChainMove) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(4 * PAGE_SIZE, 0, &vmo), ZX_OK);

  PhysMem phys_mem;
  ASSERT_EQ(phys_mem.Init(std::move(vmo)), ZX_OK);
  VirtioQueue queue;
  queue.set_phys_mem(&phys_mem);
  queue.set_interrupt([](uint8_t) { return ZX_OK; });
  queue.Configure(16, 0, PAGE_SIZE, 2 * PAGE_SIZE);

  // Test the valid chain ctor.
  VirtioChain chain1(&queue, 1);
  ASSERT_TRUE(chain1.IsValid());

  // Test the invalid chain ctor.
  VirtioChain chain2;
  ASSERT_FALSE(chain2.IsValid());

  // Test the move ctor.
  VirtioChain chain3(std::move(chain1));
  ASSERT_FALSE(chain1.IsValid());
  ASSERT_TRUE(chain3.IsValid());

  // Test the move assignment operator.
  VirtioChain chain4;
  chain4 = std::move(chain3);
  ASSERT_FALSE(chain3.IsValid());
  ASSERT_TRUE(chain4.IsValid());

  chain4.Return();
}

TEST(VirtioQueueTest, VirtioReadDesc) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(4 * PAGE_SIZE, 0, &vmo), ZX_OK);

  PhysMem phys_mem;
  ASSERT_EQ(phys_mem.Init(std::move(vmo)), ZX_OK);
  VirtioQueue queue;
  queue.set_phys_mem(&phys_mem);
  queue.Configure(1, 0, PAGE_SIZE, 2 * PAGE_SIZE);

  VirtioDescriptor desc;

  ASSERT_EQ(queue.ReadDesc(0, &desc), ZX_OK);
  ASSERT_EQ(queue.ReadDesc(2, &desc), ZX_ERR_OUT_OF_RANGE);
}

}  // namespace
