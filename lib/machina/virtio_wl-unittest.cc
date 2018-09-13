// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_wl.h"

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "lib/gtest/test_loop_fixture.h"

namespace machina {
namespace {

static constexpr uint16_t kVirtioWlQueueSize = 32;
static constexpr uint32_t kVirtioWlVmarSize = 1 << 16;
static constexpr uint32_t kAllocateFlags =
    ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;

class VirtioWlTest : public ::gtest::TestLoopFixture {
 public:
  VirtioWlTest()
      : wl_(phys_mem_, zx::vmar(), dispatcher()),
        in_queue_(wl_.in_queue(), kVirtioWlQueueSize),
        out_queue_(wl_.out_queue(), kVirtioWlQueueSize) {}

  void SetUp() override {
    uintptr_t vmar_addr;
    ASSERT_EQ(
        zx::vmar::root_self()->allocate(0u, kVirtioWlVmarSize, kAllocateFlags,
                                        wl_.vmar(), &vmar_addr),
        ZX_OK);
    ASSERT_EQ(ZX_OK, wl_.Init());
  }

  zx_status_t CreateNew(uint32_t vfd_id) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW;
    request.vfd_id = vfd_id;
    request.size = PAGE_SIZE;
    virtio_wl_ctrl_vfd_new_t response = {};
    zx_status_t status = out_queue_.BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWritable(&response, sizeof(response))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(out_queue_.HasUsed());
    EXPECT_EQ(sizeof(response), out_queue_.NextUsed().len);
    return response.hdr.type == VIRTIO_WL_RESP_VFD_NEW ? ZX_OK
                                                       : ZX_ERR_INTERNAL;
  }

 protected:
  PhysMemFake phys_mem_;
  VirtioWl wl_;
  VirtioQueueFake in_queue_;
  VirtioQueueFake out_queue_;
};

TEST_F(VirtioWlTest, HandleNew) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW;
  request.vfd_id = 1u;
  request.size = 4000u;
  virtio_wl_ctrl_vfd_new_t response = {};
  ASSERT_EQ(out_queue_.BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWritable(&response, sizeof(response))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(out_queue_.HasUsed());
  EXPECT_EQ(sizeof(response), out_queue_.NextUsed().len);
  EXPECT_EQ(response.hdr.type, VIRTIO_WL_RESP_VFD_NEW);
  EXPECT_EQ(response.hdr.flags, 0u);
  EXPECT_EQ(response.vfd_id, 1u);
  EXPECT_EQ(response.flags,
            static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));
  EXPECT_GT(response.pfn, 0u);
  EXPECT_EQ(response.size, static_cast<uint32_t>(PAGE_SIZE));
}

TEST_F(VirtioWlTest, HandleClose) {
  ASSERT_EQ(CreateNew(1u), ZX_OK);

  virtio_wl_ctrl_vfd_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_CLOSE;
  request.vfd_id = 1u;
  virtio_wl_ctrl_hdr_t response = {};
  ASSERT_EQ(out_queue_.BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWritable(&response, sizeof(response))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(out_queue_.HasUsed());
  EXPECT_EQ(sizeof(response), out_queue_.NextUsed().len);
  EXPECT_EQ(response.type, VIRTIO_WL_RESP_OK);
}

}  // namespace
}  // namespace machina
