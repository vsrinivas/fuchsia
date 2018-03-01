// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/ethernet.h>

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_net.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 8u

namespace machina {
namespace {

class VirtioNetTest : public testing::Test {
 public:
  VirtioNetTest() : net_(phys_mem_, loop_.async()), queue_(net_.rx_queue()) {}

  void SetUp() override {
    ASSERT_EQ(queue_.Init(QUEUE_SIZE), ZX_OK);
    ASSERT_EQ(zx_fifo_create(QUEUE_SIZE, sizeof(eth_fifo_entry_t), 0,
                             &fifos_.rx_fifo, &fifo_[0]),
              ZX_OK);
    ASSERT_EQ(zx_fifo_create(QUEUE_SIZE, sizeof(eth_fifo_entry_t), 0,
                             &fifos_.tx_fifo, &fifo_[1]),
              ZX_OK);
    fifos_.rx_depth = QUEUE_SIZE;
    fifos_.tx_depth = QUEUE_SIZE;
    ASSERT_EQ(net_.WaitOnFifos(fifos_), ZX_OK);
  }

 protected:
  async::Loop loop_;
  PhysMemFake phys_mem_;
  VirtioNet net_;
  VirtioQueueFake queue_;
  // Fifo entpoints to provide to the net device.
  eth_fifos_t fifos_;
  // Fifo endpoints to simulate ethernet device activity.
  zx_handle_t fifo_[2];
};

TEST_F(VirtioNetTest, DrainQueue) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor().AppendReadable(&hdr, sizeof(hdr)).Build(),
            ZX_OK);

  // Drain the queue, this will pull a descriptor from the queue and deposit
  // an entry in the fifo.
  uint32_t count;
  eth_fifo_entry_t entry[fifos_.rx_depth];
  ASSERT_EQ(ZX_OK, net_.DrainQueue(net_.rx_queue(), fifos_.rx_depth,
                                   fifos_.rx_fifo));

  // We should have no work at this point as all the buffers will be owned by
  // the ethernet device.
  loop_.RunUntilIdle();
  ASSERT_EQ(0u, net_.rx_queue()->ring()->used->idx);

  // Return a descriptor to the queue, this should trigger it to be returned.
  ASSERT_EQ(ZX_OK, zx_fifo_read(fifo_[0], entry, sizeof(entry), &count));
  ASSERT_EQ(1u, count);
  ASSERT_EQ(ZX_OK,
            zx_fifo_write(fifo_[0], &entry[0], sizeof(entry[0]), &count));
  ASSERT_EQ(1u, count);

  // Run the async tasks, verify buffers are returned.
  loop_.RunUntilIdle();
  ASSERT_EQ(1u, net_.rx_queue()->ring()->used->idx);
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

  ASSERT_EQ(zx_handle_close(fifo_[0]), ZX_OK);
  ASSERT_EQ(
      net_.DrainQueue(net_.rx_queue(), fifos_.rx_depth, fifos_.rx_fifo),
      ZX_ERR_PEER_CLOSED);
}

}  // namespace
}  // namespace machina
