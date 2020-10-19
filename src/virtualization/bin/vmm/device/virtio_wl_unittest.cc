// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/virtualization/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/zx/socket.h>
#include <string.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <virtio/wl.h>

#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

namespace {

#define VIRTWL_VQ_IN 0
#define VIRTWL_VQ_OUT 1
#define VIRTWL_NEXT_VFD_ID_BASE 0x40000000

static constexpr char kVirtioWlUrl[] = "fuchsia-pkg://fuchsia.com/virtio_wl#meta/virtio_wl.cmx";
static constexpr uint16_t kNumQueues = 2;
static constexpr uint16_t kQueueSize = 32;
static constexpr uint32_t kVirtioWlVmarSize = 1 << 16;
static constexpr uint32_t kAllocateFlags = ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;
static constexpr uint32_t kImportVmoSize = 4096;

class TestWaylandDispatcher : public fuchsia::virtualization::WaylandDispatcher {
 public:
  TestWaylandDispatcher(fit::function<void(zx::channel)> callback)
      : callback_(std::move(callback)) {}

  fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher> Bind() {
    return binding_.NewBinding();
  }

 private:
  void OnNewConnection(zx::channel channel) { callback_(std::move(channel)); }

  fit::function<void(zx::channel)> callback_;
  fidl::Binding<fuchsia::virtualization::WaylandDispatcher> binding_{this};
};

class VirtioWlTest : public TestWithDevice {
 public:
  VirtioWlTest()
      : wl_dispatcher_([this](zx::channel channel) { channels_.emplace_back(std::move(channel)); }),
        in_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        out_queue_(phys_mem_, in_queue_.end(), kQueueSize) {}

  void SetUp() override {
    uintptr_t vmar_addr;
    zx::vmar vmar;
    ASSERT_EQ(
        zx::vmar::root_self()->allocate2(kAllocateFlags, 0u, kVirtioWlVmarSize, &vmar, &vmar_addr),
        ZX_OK);
    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = LaunchDevice(kVirtioWlUrl, out_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(wl_.NewRequest());
    RunLoopUntilIdle();

    wl_->Start(std::move(start_info), std::move(vmar), wl_dispatcher_.Bind());
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&in_queue_, &out_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = wl_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }
  }

  zx_status_t CreateNew(uint32_t vfd_id, uint8_t byte) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW;
    request.vfd_id = vfd_id;
    request.size = PAGE_SIZE;
    virtio_wl_ctrl_vfd_new_t* response;
    uint16_t descriptor_id;
    zx_status_t status = DescriptorChainBuilder(out_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build(&descriptor_id);
    if (status != ZX_OK) {
      return status;
    }
    status = wl_->NotifyQueue(VIRTWL_VQ_OUT);
    if (status != ZX_OK) {
      return status;
    }
    status = WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }

    auto used_elem = NextUsed(&out_queue_);
    if (!used_elem || used_elem->id != descriptor_id || used_elem->len != sizeof(*response) ||
        response->hdr.type != VIRTIO_WL_RESP_VFD_NEW || !response->pfn ||
        response->size != PAGE_SIZE) {
      return ZX_ERR_INTERNAL;
    }

    memset(reinterpret_cast<void*>(response->pfn * PAGE_SIZE), byte, PAGE_SIZE);
    return ZX_OK;
  }

  zx_status_t CreateConnection(uint32_t vfd_id) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_CTX;
    request.vfd_id = vfd_id;
    virtio_wl_ctrl_vfd_new_t* response;
    uint16_t descriptor_id;
    zx_status_t status = DescriptorChainBuilder(out_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build(&descriptor_id);
    if (status != ZX_OK) {
      return status;
    }
    status = wl_->NotifyQueue(VIRTWL_VQ_OUT);
    if (status != ZX_OK) {
      return status;
    }
    status = WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }

    auto used_elem = NextUsed(&out_queue_);
    return (used_elem && used_elem->id == descriptor_id && used_elem->len == sizeof(*response) &&
            response->hdr.type == VIRTIO_WL_RESP_VFD_NEW)
               ? ZX_OK
               : ZX_ERR_INTERNAL;
  }

  zx_status_t CreatePipe(uint32_t vfd_id) {
    virtio_wl_ctrl_vfd_new_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_PIPE;
    request.vfd_id = vfd_id;
    request.flags = VIRTIO_WL_VFD_READ;
    virtio_wl_ctrl_vfd_new_t* response;
    uint16_t descriptor_id;
    zx_status_t status = DescriptorChainBuilder(out_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build(&descriptor_id);
    if (status != ZX_OK) {
      return status;
    }
    status = wl_->NotifyQueue(VIRTWL_VQ_OUT);
    if (status != ZX_OK) {
      return status;
    }
    status = WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }

    auto used_elem = NextUsed(&out_queue_);
    return (used_elem && used_elem->id == descriptor_id && used_elem->len == sizeof(*response) &&
            response->hdr.type == VIRTIO_WL_RESP_VFD_NEW)
               ? ZX_OK
               : ZX_ERR_INTERNAL;
  }

  std::optional<VirtioQueueFake::UsedElement> NextUsed(VirtioQueueFake* queue) {
    auto elem = queue->NextUsed();
    while (!elem && WaitOnInterrupt() == ZX_OK) {
      elem = queue->NextUsed();
    }
    return elem;
  }

 protected:
  TestWaylandDispatcher wl_dispatcher_;
  fuchsia::virtualization::hardware::VirtioWaylandSyncPtr wl_;
  VirtioQueueFake in_queue_;
  VirtioQueueFake out_queue_;
  std::vector<zx::channel> channels_;
};

TEST_F(VirtioWlTest, HandleNew) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW;
  request.vfd_id = 1u;
  request.size = 4000u;
  virtio_wl_ctrl_vfd_new_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);

  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->hdr.type, VIRTIO_WL_RESP_VFD_NEW);
  EXPECT_EQ(response->hdr.flags, 0u);
  EXPECT_EQ(response->vfd_id, 1u);
  EXPECT_EQ(response->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));
  EXPECT_GT(response->pfn, 0u);
  EXPECT_EQ(response->size, static_cast<uint32_t>(PAGE_SIZE));
  memset(reinterpret_cast<void*>(response->pfn * PAGE_SIZE), 0xff, 4000u);
}

TEST_F(VirtioWlTest, HandleClose) {
  ASSERT_EQ(CreateNew(1u, 0xff), ZX_OK);

  virtio_wl_ctrl_vfd_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_CLOSE;
  request.vfd_id = 1u;
  virtio_wl_ctrl_hdr_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->type, VIRTIO_WL_RESP_OK);
}

TEST_F(VirtioWlTest, HandleNewCtx) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_CTX;
  request.vfd_id = 1u;
  virtio_wl_ctrl_vfd_new_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->hdr.type, VIRTIO_WL_RESP_VFD_NEW);
  EXPECT_EQ(response->hdr.flags, 0u);
  EXPECT_EQ(response->vfd_id, 1u);
  EXPECT_EQ(response->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));

  RunLoopUntilIdle();
  EXPECT_EQ(channels_.size(), 1u);
  channels_.clear();
}

TEST_F(VirtioWlTest, HandleNewPipe) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_PIPE;
  request.vfd_id = 1u;
  request.flags = VIRTIO_WL_VFD_READ;
  virtio_wl_ctrl_vfd_new_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->hdr.type, VIRTIO_WL_RESP_VFD_NEW);
  EXPECT_EQ(response->hdr.flags, 0u);
  EXPECT_EQ(response->vfd_id, 1u);
  EXPECT_EQ(response->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ));
}

TEST_F(VirtioWlTest, HandleDmabuf) {
  virtio_wl_ctrl_vfd_new_t request = {};
  request.hdr.type = VIRTIO_WL_CMD_VFD_NEW_DMABUF;
  request.vfd_id = 1u;
  request.dmabuf.format = 0x34325241;  // DRM_FORMAT_ARGB8888
  request.dmabuf.width = 64;
  request.dmabuf.height = 64;
  virtio_wl_ctrl_vfd_new_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->hdr.type, VIRTIO_WL_RESP_VFD_NEW_DMABUF);
  EXPECT_EQ(response->hdr.flags, 0u);
  EXPECT_EQ(response->vfd_id, 1u);
  EXPECT_EQ(response->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));
  EXPECT_GT(response->pfn, 0u);
  EXPECT_GT(response->size, 0u);
}

TEST_F(VirtioWlTest, HandleSend) {
  ASSERT_EQ(CreateNew(1u, 0xaa), ZX_OK);
  ASSERT_EQ(CreatePipe(2u), ZX_OK);
  ASSERT_EQ(CreateConnection(3u), ZX_OK);

  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);

  uint8_t request[sizeof(virtio_wl_ctrl_vfd_send_t) + sizeof(uint32_t) * 3];
  virtio_wl_ctrl_vfd_send_t* header = reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(request);
  header->hdr.type = VIRTIO_WL_CMD_VFD_SEND;
  header->vfd_id = 3u;
  header->vfd_count = 2u;
  uint32_t* vfds = reinterpret_cast<uint32_t*>(header + 1);
  vfds[0] = 1u;
  vfds[1] = 2u;
  vfds[2] = 1234u;  // payload
  virtio_wl_ctrl_hdr_t* response;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&request, sizeof(request))
                .AppendWritableDescriptor(&response, sizeof(*response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  auto used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*response));
  EXPECT_EQ(response->type, VIRTIO_WL_RESP_OK);

  uint32_t data;
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(zx_channel_read(channels_[0].get(), 0, &data, handles, sizeof(data), std::size(handles),
                            &actual_bytes, &actual_handles),
            ZX_OK);
  EXPECT_EQ(actual_handles, 2u);
  EXPECT_EQ(actual_bytes, sizeof(data));
  EXPECT_EQ(data, 1234u);

  zx::vmo vmo(handles[0]);
  zx::socket socket(handles[1]);

  // Verify data transfer using shared memory.
  uintptr_t addr;
  ASSERT_EQ(
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, PAGE_SIZE, &addr),
      ZX_OK);
  EXPECT_EQ(*reinterpret_cast<uint8_t*>(addr), 0xaa);
  ASSERT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  // Verify data transfer over pipe.
  size_t actual_size;
  ASSERT_EQ(socket.write(0, &data, sizeof(data), &actual_size), ZX_OK);
  EXPECT_EQ(actual_size, sizeof(data));
  RunLoopUntilIdle();

  size_t buffer_size = sizeof(virtio_wl_ctrl_vfd_recv_t) + sizeof(data);
  uint8_t* buffer;
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&buffer, buffer_size)
                .Build(&descriptor_id),
            ZX_OK);
  virtio_wl_ctrl_vfd_recv_t* recv_header = reinterpret_cast<virtio_wl_ctrl_vfd_recv_t*>(buffer);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_IN), ZX_OK);
  used_elem = NextUsed(&in_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, buffer_size);
  EXPECT_EQ(recv_header->hdr.type, VIRTIO_WL_CMD_VFD_RECV);
  EXPECT_EQ(recv_header->hdr.flags, 0u);
  EXPECT_EQ(recv_header->vfd_id, 2u);
  EXPECT_EQ(recv_header->vfd_count, 0u);
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(recv_header + 1), 1234u);

  channels_.clear();
}

TEST_F(VirtioWlTest, Recv) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(PAGE_SIZE, 0, &vmo), ZX_OK);
  uintptr_t addr;
  ASSERT_EQ(
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, PAGE_SIZE, &addr),
      ZX_OK);
  memset(reinterpret_cast<void*>(addr), 0xaa, PAGE_SIZE);
  ASSERT_EQ(zx::vmar::root_self()->unmap(addr, PAGE_SIZE), ZX_OK);

  zx::socket socket, remote_socket;
  ASSERT_EQ(zx::socket::create(0, &socket, &remote_socket), ZX_OK);

  uint32_t data = 1234u;
  zx_handle_t handles[] = {vmo.release(), remote_socket.release()};
  ASSERT_EQ(
      zx_channel_write(channels_[0].get(), 0, &data, sizeof(data), handles, std::size(handles)),
      ZX_OK);
  RunLoopUntilIdle();

  virtio_wl_ctrl_vfd_new_t* new_vfd_cmd[2];
  size_t buffer_size =
      sizeof(virtio_wl_ctrl_vfd_recv_t) + sizeof(uint32_t) * std::size(handles) + sizeof(data);
  uint8_t* buffer;
  uint16_t descriptor_id[3];
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&buffer, buffer_size)
                .Build(&descriptor_id[0]),
            ZX_OK);
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&new_vfd_cmd[0], sizeof(virtio_wl_ctrl_vfd_new_t))
                .Build(&descriptor_id[1]),
            ZX_OK);
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&new_vfd_cmd[1], sizeof(virtio_wl_ctrl_vfd_new_t))
                .Build(&descriptor_id[2]),
            ZX_OK);
  virtio_wl_ctrl_vfd_recv_t* header = reinterpret_cast<virtio_wl_ctrl_vfd_recv_t*>(buffer);
  uint32_t* vfds = reinterpret_cast<uint32_t*>(header + 1);
  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_IN), ZX_OK);

  // Descriptors should be returned in the order:
  //   descriptor_id[1] -> NEW_VFD (VMO)
  //   descriptor_id[2] -> NEW_VFD (SOCKET)
  //   descriptor_id[0] -> RECV
  // This is because the RECV message is read out of the channel directly into
  // descriptor_id[0], but then we see the new VFDs that need to be created
  // first.

  // descriptor_id[1] -> NEW_VFD (VMO)
  auto used_elem = NextUsed(&in_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id[1]);
  EXPECT_EQ(used_elem->len, sizeof(virtio_wl_ctrl_vfd_new_t));

  // descriptor_id[2] -> NEW_VFD (SOCKET)
  used_elem = NextUsed(&in_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id[2]);
  EXPECT_EQ(used_elem->len, sizeof(virtio_wl_ctrl_vfd_new_t));

  // descriptor_id[0] -> RECV
  used_elem = NextUsed(&in_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id[0]);
  EXPECT_EQ(used_elem->len, buffer_size);

  EXPECT_EQ(new_vfd_cmd[0]->hdr.type, VIRTIO_WL_CMD_VFD_NEW);
  EXPECT_EQ(new_vfd_cmd[0]->hdr.flags, 0u);
  EXPECT_EQ(new_vfd_cmd[0]->vfd_id, static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE));
  EXPECT_EQ(new_vfd_cmd[0]->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));
  // We use memcpy to avoid an unaligned access, due to the packed structure.
  uint64_t pfn;
  memcpy(&pfn, &new_vfd_cmd[0]->pfn, sizeof(pfn));
  EXPECT_GT(pfn, 0u);
  EXPECT_EQ(new_vfd_cmd[0]->size, static_cast<uint32_t>(PAGE_SIZE));
  EXPECT_EQ(*reinterpret_cast<uint8_t*>(new_vfd_cmd[0]->pfn * PAGE_SIZE), 0xaa);

  EXPECT_EQ(new_vfd_cmd[1]->hdr.type, VIRTIO_WL_CMD_VFD_NEW_PIPE);
  EXPECT_EQ(new_vfd_cmd[1]->hdr.flags, 0u);
  EXPECT_EQ(new_vfd_cmd[1]->vfd_id, static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE + 1));
  EXPECT_EQ(new_vfd_cmd[1]->flags, static_cast<uint32_t>(VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE));

  EXPECT_EQ(header->hdr.type, VIRTIO_WL_CMD_VFD_RECV);
  EXPECT_EQ(header->hdr.flags, 0u);
  EXPECT_EQ(header->vfd_id, 1u);
  EXPECT_EQ(header->vfd_count, 2u);
  EXPECT_EQ(vfds[0], static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE));
  EXPECT_EQ(vfds[1], static_cast<uint32_t>(VIRTWL_NEXT_VFD_ID_BASE + 1));
  EXPECT_EQ(*reinterpret_cast<uint32_t*>(vfds + std::size(handles)), 1234u);

  {  // Check that closing shared memory works as expected.
    uint16_t descriptor_id;
    virtio_wl_ctrl_vfd_t request = {};
    request.hdr.type = VIRTIO_WL_CMD_VFD_CLOSE;
    request.vfd_id = VIRTWL_NEXT_VFD_ID_BASE;
    virtio_wl_ctrl_hdr_t* response;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response, sizeof(*response))
                  .Build(&descriptor_id),
              ZX_OK);

    ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
    used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(*response));
    EXPECT_EQ(response->type, VIRTIO_WL_RESP_OK);
  }

  {  // Check that writing to pipe works as expected.
    uint16_t descriptor_id = 0;
    uint8_t send_request[sizeof(virtio_wl_ctrl_vfd_send_t) + sizeof(uint32_t)];
    virtio_wl_ctrl_vfd_send_t* send_header =
        reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(send_request);
    send_header->hdr.type = VIRTIO_WL_CMD_VFD_SEND;
    send_header->vfd_id = VIRTWL_NEXT_VFD_ID_BASE + 1;
    send_header->vfd_count = 0;
    *reinterpret_cast<uint32_t*>(send_header + 1) = 1234u;  // payload
    virtio_wl_ctrl_hdr_t* send_response;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&send_request, sizeof(send_request))
                  .AppendWritableDescriptor(&send_response, sizeof(*send_response))
                  .Build(&descriptor_id),
              ZX_OK);

    ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
    used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(*send_response));
    EXPECT_EQ(send_response->type, VIRTIO_WL_RESP_OK);

    uint32_t pipe_data;
    size_t actual_bytes;
    ASSERT_EQ(socket.read(0, &pipe_data, sizeof(pipe_data), &actual_bytes), ZX_OK);
    EXPECT_EQ(actual_bytes, sizeof(pipe_data));
    EXPECT_EQ(pipe_data, 1234u);
  }

  channels_.clear();
}

TEST_F(VirtioWlTest, Hup) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);

  // Close remote side of channel.
  channels_.clear();
  RunLoopUntilIdle();

  virtio_wl_ctrl_vfd_t* header;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&header, sizeof(*header))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_IN), ZX_OK);
  auto used_elem = NextUsed(&in_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*header));
  EXPECT_EQ(header->hdr.type, VIRTIO_WL_CMD_VFD_HUP);
  EXPECT_EQ(header->hdr.flags, 0u);
  EXPECT_EQ(header->vfd_id, 1u);
}

TEST_F(VirtioWlTest, Import) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);
  fuchsia::virtualization::hardware::VirtioWaylandImporterSyncPtr importer;
  ASSERT_EQ(wl_->GetImporter(importer.NewRequest()), ZX_OK);
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kImportVmoSize, 0u, &vmo), ZX_OK);
  uint32_t vfd_id = 0;
  ASSERT_EQ(importer->Import(std::move(vmo), &vfd_id), ZX_OK);
  ASSERT_NE(vfd_id, 0u);
}

}  // namespace
