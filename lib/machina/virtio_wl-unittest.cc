// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_wl.h"

#include <lib/zx/socket.h>
#include <string.h>

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
      : wl_(phys_mem_, zx::vmar(), dispatcher(),
            [this](zx::channel channel) {
              channels_.emplace_back(std::move(channel));
            }),
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

  zx_status_t CreateNew(uint32_t vfd_id, uint8_t byte) {
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
    if (!out_queue_.HasUsed() ||
        out_queue_.NextUsed().len != sizeof(response) ||
        response.hdr.type != VIRTIO_WL_RESP_VFD_NEW || !response.pfn ||
        response.size != PAGE_SIZE) {
      return ZX_ERR_INTERNAL;
    }

    memset(reinterpret_cast<void*>(response.pfn * PAGE_SIZE), byte, PAGE_SIZE);
    return ZX_OK;
  }

  zx_status_t CreateConnection(uint32_t vfd_id) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_CTX;
    request.vfd_id = vfd_id;
    virtio_wl_ctrl_vfd_new_t response = {};
    zx_status_t status = out_queue_.BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWritable(&response, sizeof(response))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    return (out_queue_.HasUsed() &&
            out_queue_.NextUsed().len == sizeof(response) &&
            response.hdr.type == VIRTIO_WL_RESP_VFD_NEW)
               ? ZX_OK
               : ZX_ERR_INTERNAL;
  }

  zx_status_t CreatePipe(uint32_t vfd_id) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_PIPE;
    request.vfd_id = vfd_id;
    virtio_wl_ctrl_vfd_new_t response = {};
    zx_status_t status = out_queue_.BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWritable(&response, sizeof(response))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    return (out_queue_.HasUsed() &&
            out_queue_.NextUsed().len == sizeof(response) &&
            response.hdr.type == VIRTIO_WL_RESP_VFD_NEW)
               ? ZX_OK
               : ZX_ERR_INTERNAL;
  }

 protected:
  PhysMemFake phys_mem_;
  VirtioWl wl_;
  VirtioQueueFake in_queue_;
  VirtioQueueFake out_queue_;
  std::vector<zx::channel> channels_;
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
  memset(reinterpret_cast<void*>(response.pfn * PAGE_SIZE), 0xff, 4000u);
}

TEST_F(VirtioWlTest, HandleClose) {
  ASSERT_EQ(CreateNew(1u, 0xff), ZX_OK);

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

TEST_F(VirtioWlTest, HandleNewCtx) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_CTX;
  request.vfd_id = 1u;
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
  EXPECT_EQ(channels_.size(), 1u);
  channels_.clear();
}

TEST_F(VirtioWlTest, HandleNewPipe) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_PIPE;
  request.vfd_id = 1u;
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
  EXPECT_EQ(response.flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ));
}

TEST_F(VirtioWlTest, HandleSend) {
  ASSERT_EQ(CreateNew(1u, 0xfe), ZX_OK);
  ASSERT_EQ(CreatePipe(2u), ZX_OK);
  ASSERT_EQ(CreateConnection(3u), ZX_OK);
  ASSERT_EQ(channels_.size(), 1u);

  uint8_t request[sizeof(virtio_wl_ctrl_vfd_send_t) + sizeof(uint32_t) * 3];
  virtio_wl_ctrl_vfd_send_t* header =
      reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(request);
  header->hdr.type = VIRTIO_WL_CMD_VFD_SEND;
  header->vfd_id = 3u;
  header->vfd_count = 2u;
  uint32_t* vfds = reinterpret_cast<uint32_t*>(header + 1);
  vfds[0] = 1u;
  vfds[1] = 2u;
  vfds[2] = 1234u;  // payload
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

  uint32_t data;
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(zx_channel_read(channels_[0].get(), 0, &data, handles, sizeof(data),
                            countof(handles), &actual_bytes, &actual_handles),
            ZX_OK);
  EXPECT_EQ(actual_handles, 2u);
  EXPECT_EQ(actual_bytes, sizeof(data));
  EXPECT_EQ(data, 1234u);

  zx::vmo vmo(handles[0]);
  zx::socket socket(handles[1]);

  // Verify data transfer using shared memory.
  uintptr_t addr;
  ASSERT_EQ(
      zx::vmar::root_self()->map(0, vmo, 0, PAGE_SIZE,
                                 ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr),
      ZX_OK);
  EXPECT_EQ(*reinterpret_cast<uint8_t*>(addr), 0xfe);
  ASSERT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  // Verify data transfer over pipe.
  size_t actual_size;
  ASSERT_EQ(socket.write(0, &data, sizeof(data), &actual_size), ZX_OK);
  EXPECT_EQ(actual_size, sizeof(data));

  channels_.clear();
}

}  // namespace
}  // namespace machina
