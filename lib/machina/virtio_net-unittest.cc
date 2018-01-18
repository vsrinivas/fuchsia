// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/ethernet.h>

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio.h"
#include "garnet/lib/machina/virtio_net.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 8u

namespace machina {
namespace {

class VirtioNetTest : public testing::Test {
 public:
  VirtioNetTest() : net_(phys_mem_), queue_(net_.rx_queue()) {}

  void SetUp() override {
    ASSERT_EQ(queue_.Init(QUEUE_SIZE), ZX_OK);
    ASSERT_EQ(zx_fifo_create(QUEUE_SIZE, sizeof(eth_fifo_entry_t), 0, &fifo_[0],
                             &fifo_[1]),
              ZX_OK);
  }

 protected:
  PhysMemFake phys_mem_;
  VirtioNet net_;
  VirtioQueueFake queue_;
  zx_handle_t fifo_[2];
};

TEST_F(VirtioNetTest, DrainQueue) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor().AppendReadable(&hdr, sizeof(hdr)).Build(),
            ZX_OK);

  uint32_t count;
  eth_fifo_entry_t entry = {};
  ASSERT_EQ(zx_fifo_write(fifo_[1], &entry, sizeof(entry), &count), ZX_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_EQ(net_.DrainQueue(net_.rx_queue(), QUEUE_SIZE, fifo_[0]), ZX_OK);
}

TEST_F(VirtioNetTest, InvalidDesc) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor()
                .AppendReadable(&hdr, sizeof(hdr))
                .AppendReadable(&hdr, sizeof(hdr))
                .Build(),
            ZX_OK);

  uint32_t count;
  eth_fifo_entry_t entry = {};
  ASSERT_EQ(zx_fifo_write(fifo_[1], &entry, sizeof(entry), &count), ZX_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_EQ(net_.DrainQueue(net_.rx_queue(), QUEUE_SIZE, fifo_[0]),
            ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(VirtioNetTest, PeerClosed) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor().AppendReadable(&hdr, sizeof(hdr)).Build(),
            ZX_OK);

  ASSERT_EQ(zx_handle_close(fifo_[1]), ZX_OK);
  ASSERT_EQ(net_.DrainQueue(net_.rx_queue(), QUEUE_SIZE, fifo_[0]),
            ZX_ERR_PEER_CLOSED);
}

}  // namespace
}  // namespace machina
