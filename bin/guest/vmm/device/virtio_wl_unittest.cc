// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_wl.h"

#include <lib/zx/socket.h>
#include <string.h>

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "lib/fxl/arraysize.h"
#include "lib/gtest/test_loop_fixture.h"

namespace machina {
namespace {

static constexpr uint16_t kVirtioWlQueueSize = 32;
static constexpr uint32_t kVirtioWlVmarSize = 1 << 16;
static constexpr uint32_t kAllocateFlags =
    ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;

class TestWaylandDispatcher : public fuchsia::guest::WaylandDispatcher {
 public:
  TestWaylandDispatcher(fit::function<void(zx::channel)> callback)
    : callback_(std::move(callback)) {}

 private:
  void OnNewConnection(zx::channel channel) {
    callback_(std::move(channel));
  }

  fit::function<void(zx::channel)> callback_;
};

class VirtioWlTest : public ::gtest::TestLoopFixture {
 public:
  VirtioWlTest()
      : wl_dispatcher_([this](zx::channel channel) {
          channels_.emplace_back(std::move(channel));
          }),
        wl_(phys_mem_, zx::vmar(), dispatcher(), &wl_dispatcher_),
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
    request.flags = VIRTIO_WL_VFD_READ;
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
  TestWaylandDispatcher wl_dispatcher_;
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
  request.flags = VIRTIO_WL_VFD_READ;
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
  ASSERT_EQ(CreateNew(1u, 0xaa), ZX_OK);
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
                            arraysize(handles), &actual_bytes, &actual_handles),
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
  EXPECT_EQ(*reinterpret_cast<uint8_t*>(addr), 0xaa);
  ASSERT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  // Verify data transfer over pipe.
  size_t actual_size;
  ASSERT_EQ(socket.write(0, &data, sizeof(data), &actual_size), ZX_OK);
  EXPECT_EQ(actual_size, sizeof(data));
  RunLoopUntilIdle();

  uint8_t buffer[sizeof(virtio_wl_ctrl_vfd_recv_t) + sizeof(data)];
  virtio_wl_ctrl_vfd_recv_t* recv_header =
      reinterpret_cast<virtio_wl_ctrl_vfd_recv_t*>(buffer);
  ASSERT_EQ(in_queue_.BuildDescriptor()
                .AppendWritable(buffer, sizeof(buffer))
                .Build(),
            ZX_OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(in_queue_.HasUsed());
  EXPECT_EQ(sizeof(buffer), in_queue_.NextUsed().len);
  EXPECT_EQ(recv_header->hdr.type, VIRTIO_WL_CMD_VFD_RECV);
  EXPECT_EQ(recv_header->hdr.flags, 0u);
  EXPECT_EQ(recv_header->vfd_id, 2u);
  EXPECT_EQ(recv_header->vfd_count, 0u);
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(recv_header + 1), 1234u);

  channels_.clear();
}

TEST_F(VirtioWlTest, Recv) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  ASSERT_EQ(channels_.size(), 1u);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(PAGE_SIZE, 0, &vmo), ZX_OK);
  uintptr_t addr;
  ASSERT_EQ(
      zx::vmar::root_self()->map(0, vmo, 0, PAGE_SIZE,
                                 ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr),
      ZX_OK);
  memset(reinterpret_cast<void*>(addr), 0xaa, PAGE_SIZE);
  ASSERT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  zx::socket socket, remote_socket;
  ASSERT_EQ(zx::socket::create(0, &socket, &remote_socket), ZX_OK);

  uint32_t data = 1234u;
  zx_handle_t handles[] = {vmo.release(), remote_socket.release()};
  ASSERT_EQ(zx_channel_write(channels_[0].get(), 0, &data, sizeof(data),
                             handles, fbl::count_of(handles)),
            ZX_OK);
  RunLoopUntilIdle();

  uint8_t buffer[sizeof(virtio_wl_ctrl_vfd_new_t) * fbl::count_of(handles) +
                 sizeof(virtio_wl_ctrl_vfd_recv_t) +
                 sizeof(uint32_t) * fbl::count_of(handles) + sizeof(data)];
  virtio_wl_ctrl_vfd_new_t* new_vfd_cmd =
      reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(buffer);
  virtio_wl_ctrl_vfd_recv_t* header =
      reinterpret_cast<virtio_wl_ctrl_vfd_recv_t*>(new_vfd_cmd +
                                                   fbl::count_of(handles));
  uint32_t* vfds = reinterpret_cast<uint32_t*>(header + 1);
  ASSERT_EQ(in_queue_.BuildDescriptor()
                .AppendWritable(buffer, sizeof(buffer))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(in_queue_.HasUsed());
  EXPECT_EQ(sizeof(buffer), in_queue_.NextUsed().len);

  EXPECT_EQ(new_vfd_cmd[0].hdr.type, VIRTIO_WL_CMD_VFD_NEW);
  EXPECT_EQ(new_vfd_cmd[0].hdr.flags, 0u);
  EXPECT_EQ(new_vfd_cmd[0].vfd_id,
            static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE));
  EXPECT_EQ(new_vfd_cmd[0].flags,
            static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));
  EXPECT_GT(new_vfd_cmd[0].pfn, 0u);
  EXPECT_EQ(new_vfd_cmd[0].size, static_cast<uint32_t>(PAGE_SIZE));
  EXPECT_EQ(*reinterpret_cast<uint8_t*>(new_vfd_cmd[0].pfn * PAGE_SIZE), 0xaa);

  EXPECT_EQ(new_vfd_cmd[1].hdr.type, VIRTIO_WL_CMD_VFD_NEW_PIPE);
  EXPECT_EQ(new_vfd_cmd[1].hdr.flags, 0u);
  EXPECT_EQ(new_vfd_cmd[1].vfd_id,
            static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE + 1));
  EXPECT_EQ(new_vfd_cmd[1].flags,
            static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));

  EXPECT_EQ(header->hdr.type, VIRTIO_WL_CMD_VFD_RECV);
  EXPECT_EQ(header->hdr.flags, 0u);
  EXPECT_EQ(header->vfd_id, 1u);
  EXPECT_EQ(header->vfd_count, 2u);
  EXPECT_EQ(vfds[0], static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE));
  EXPECT_EQ(vfds[1], static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE + 1));
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(vfds + fbl::count_of(handles)), 1234u);

  // Check that closing shared memory works as expected.
  virtio_wl_ctrl_vfd_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_CLOSE;
  request.vfd_id = VIRTWL_NEXT_VFD_ID_BASE;
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

  // Check that writing to pipe works as expected.
  uint8_t send_request[sizeof(virtio_wl_ctrl_vfd_send_t) + sizeof(uint32_t)];
  virtio_wl_ctrl_vfd_send_t* send_header =
      reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(send_request);
  send_header->hdr.type = VIRTIO_WL_CMD_VFD_SEND;
  send_header->vfd_id = VIRTWL_NEXT_VFD_ID_BASE + 1;
  send_header->vfd_count = 0;
  *reinterpret_cast<uint32_t*>(send_header + 1) = 1234u;  // payload
  virtio_wl_ctrl_hdr_t send_response = {};
  ASSERT_EQ(out_queue_.BuildDescriptor()
                .AppendReadable(&send_request, sizeof(send_request))
                .AppendWritable(&send_response, sizeof(send_response))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(out_queue_.HasUsed());
  EXPECT_EQ(sizeof(send_response), out_queue_.NextUsed().len);
  EXPECT_EQ(send_response.type, VIRTIO_WL_RESP_OK);

  uint32_t pipe_data;
  size_t actual_bytes;
  ASSERT_EQ(socket.read(0, &pipe_data, sizeof(pipe_data), &actual_bytes),
            ZX_OK);
  EXPECT_EQ(actual_bytes, sizeof(pipe_data));
  EXPECT_EQ(pipe_data, 1234u);

  channels_.clear();
}

TEST_F(VirtioWlTest, Hup) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  ASSERT_EQ(channels_.size(), 1u);

  // Close remote side of channel.
  channels_.clear();
  RunLoopUntilIdle();

  virtio_wl_ctrl_vfd_t header = {};
  ASSERT_EQ(in_queue_.BuildDescriptor()
                .AppendWritable(&header, sizeof(header))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(in_queue_.HasUsed());
  EXPECT_EQ(sizeof(header), in_queue_.NextUsed().len);
  EXPECT_EQ(header.hdr.type, VIRTIO_WL_CMD_VFD_HUP);
  EXPECT_EQ(header.hdr.flags, 0u);
  EXPECT_EQ(header.vfd_id, 1u);
}

}  // namespace
}  // namespace machina
