// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/virtio_net_legacy.h"

#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include "src/virtualization/bin/vmm/phys_mem_fake.h"
#include "src/virtualization/bin/vmm/virtio_queue_fake.h"

namespace {

static constexpr uint16_t kVirtioNetQueueSize = 8;

class VirtioNetFake : public VirtioNetLegacy {
 public:
  VirtioNetFake(const PhysMem& phys_mem, async_dispatcher_t* dispatcher)
      : VirtioNetLegacy(phys_mem, dispatcher) {}

  void SetUp(const fuchsia_hardware_ethernet_Fifos& fifos) {
    ASSERT_EQ(InitIoBuffer(kVirtioNetQueueSize * 2, 2048), ZX_OK);
    ASSERT_EQ(WaitOnFifos(fifos), ZX_OK);
  }
};

class VirtioNetTest : public ::gtest::TestLoopFixture {
 public:
  VirtioNetTest()
      : net_(phys_mem_, dispatcher()),
        queue_(net_.rx_queue(), kVirtioNetQueueSize) {}

  void SetUp() override {
    ASSERT_EQ(zx_fifo_create(kVirtioNetQueueSize, sizeof(eth_fifo_entry_t), 0,
                             &fifos_.rx, &fifo_[0]),
              ZX_OK);
    ASSERT_EQ(zx_fifo_create(kVirtioNetQueueSize, sizeof(eth_fifo_entry_t), 0,
                             &fifos_.tx, &fifo_[1]),
              ZX_OK);
    fifos_.rx_depth = kVirtioNetQueueSize;
    fifos_.tx_depth = kVirtioNetQueueSize;
    net_.SetUp(fifos_);
  }

 protected:
  PhysMemFake phys_mem_;
  VirtioNetFake net_;
  VirtioQueueFake queue_;
  // Fifo endpoints to provide to the net device.
  fuchsia_hardware_ethernet_Fifos fifos_;
  // Fifo endpoints to simulate ethernet device activity.
  zx_handle_t fifo_[2];
};

TEST_F(VirtioNetTest, DrainQueue) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor().AppendReadable(&hdr, sizeof(hdr)).Build(),
            ZX_OK);

  // Drain the queue, this will pull a descriptor from the queue and deposit
  // an entry in the fifo.
  size_t count;
  eth_fifo_entry_t entry[kVirtioNetQueueSize];

  // We should have no work at this point as all the buffers will be owned by
  // the ethernet device.
  RunLoopUntilIdle();
  ASSERT_EQ(0u, queue_.ring()->used->idx);

  // Return a descriptor to the queue, this should trigger it to be returned.
  ASSERT_EQ(ZX_OK, zx_fifo_read(fifo_[0], sizeof(entry[0]), entry,
                                kVirtioNetQueueSize, &count));
  ASSERT_EQ(1u, count);
  ASSERT_EQ(ZX_OK,
            zx_fifo_write(fifo_[0], sizeof(entry[0]), &entry[0], 1, nullptr));

  // Run the async tasks, verify buffers are returned.
  RunLoopUntilIdle();
  ASSERT_EQ(1u, queue_.ring()->used->idx);
}

TEST_F(VirtioNetTest, HeaderOnDifferentBuffer) {
  virtio_net_hdr_t hdr = {};
  // Ethernet FIFOs only support 32-bit VMO offsets which means validating
  // against stack values may not be safe if they're above UINT32_MAX in our
  // address space.
  uint8_t* packet_ptr = reinterpret_cast<uint8_t*>(0x123456);
  size_t packet_len = 512;
  ASSERT_EQ(queue_.BuildDescriptor()
                .AppendReadable(&hdr, sizeof(hdr))
                .AppendReadable(packet_ptr, packet_len)
                .Build(),
            ZX_OK);
  RunLoopUntilIdle();

  size_t count;
  eth_fifo_entry_t entry[kVirtioNetQueueSize];

  // Read the fifo entry.
  ASSERT_EQ(ZX_OK, zx_fifo_read(fifo_[0], sizeof(entry[0]), entry,
                                kVirtioNetQueueSize, &count));
  ASSERT_EQ(1u, count);
  // Expect the first offset in the IoBuffer to be allocated
  ASSERT_EQ(0u, entry[0].offset);
  ASSERT_EQ(packet_len, entry[0].length);
}

TEST_F(VirtioNetTest, InvalidDesc) {
  virtio_net_hdr_t hdr = {};
  uint8_t packet[1024];
  ASSERT_EQ(queue_.BuildDescriptor()
                .AppendReadable(&hdr, sizeof(hdr))
                .AppendReadable(packet, sizeof(packet))
                .AppendReadable(packet, sizeof(packet))
                .Build(),
            ZX_OK);

  // Expect nothing is written to the FIFO.
  RunLoopUntilIdle();
  eth_fifo_entry_t entry[kVirtioNetQueueSize];
  ASSERT_EQ(zx_fifo_read(fifo_[0], sizeof(entry[0]), entry, kVirtioNetQueueSize,
                         nullptr),
            ZX_ERR_SHOULD_WAIT);
}

TEST_F(VirtioNetTest, PeerClosed) {
  virtio_net_hdr_t hdr = {};
  ASSERT_EQ(queue_.BuildDescriptor().AppendReadable(&hdr, sizeof(hdr)).Build(),
            ZX_OK);
  ASSERT_EQ(zx_handle_close(fifo_[0]), ZX_OK);
  ASSERT_EQ(zx_handle_close(fifo_[1]), ZX_OK);
  RunLoopUntilIdle();
}

}  // namespace
