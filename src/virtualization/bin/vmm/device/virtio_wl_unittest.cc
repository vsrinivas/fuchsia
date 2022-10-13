// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <drm_fourcc.h>
#include <fuchsia/sysmem/cpp/fidl_test_base.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl_test_base.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <fuchsia/wayland/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/zx/socket.h>
#include <string.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <virtio/wl.h>

#include "fuchsia/logger/cpp/fidl.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

#define VIRTWL_VQ_IN 0
#define VIRTWL_VQ_OUT 1
#define VIRTWL_NEXT_VFD_ID_BASE 0x40000000

static constexpr uint16_t kNumQueues = 2;
static constexpr uint16_t kQueueSize = 32;
static constexpr uint32_t kVirtioWlVmarSize = 1 << 16;
static constexpr uint32_t kAllocateFlags = ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;
static constexpr uint32_t kImportVmoSize = 4096;
static constexpr uint32_t kDmabufWidth = 64;
static constexpr uint32_t kDmabufHeight = 64;
static constexpr uint32_t kDmabufDrmFormat = DRM_FORMAT_ARGB8888;
static constexpr fuchsia::sysmem::PixelFormatType kDmabufSysmemFormat =
    fuchsia::sysmem::PixelFormatType::BGRA32;

class TestWaylandDispatcher : public fuchsia::wayland::Server {
 public:
  TestWaylandDispatcher(fit::function<void(zx::channel)> callback)
      : callback_(std::move(callback)) {}

  fidl::InterfaceHandle<fuchsia::wayland::Server> Bind() { return binding_.NewBinding(); }

 private:
  void Connect(zx::channel channel) { callback_(std::move(channel)); }

  fit::function<void(zx::channel)> callback_;
  fidl::Binding<fuchsia::wayland::Server> binding_{this};
};

class TestBufferCollectionToken : public fuchsia::sysmem::testing::BufferCollectionToken_TestBase {
 public:
  TestBufferCollectionToken(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request)
      : binding_{this, std::move(request)} {}

 private:
  // |fuchsia::sysmem::BufferCollection|
  void Duplicate(uint32_t rights_attenuation_mask,
                 fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request) override {
    duplicates_.emplace_back(std::make_unique<TestBufferCollectionToken>(std::move(request)));
  }
  void Sync(SyncCallback callback) override { callback(); }

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented BufferCollection." << name;
  }

  std::vector<std::unique_ptr<TestBufferCollectionToken>> duplicates_;
  fidl::Binding<fuchsia::sysmem::BufferCollectionToken> binding_;
};

class TestBufferCollection : public fuchsia::sysmem::testing::BufferCollection_TestBase {
 public:
  TestBufferCollection(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request)
      : binding_{this, std::move(request)} {}

 private:
  // |fuchsia::sysmem::BufferCollection|
  void Close() override { binding_.Close(ZX_OK); }
  void SetName(uint32_t priority, std::string name) override {}
  void SetConstraints(bool has_constraints,
                      fuchsia::sysmem::BufferCollectionConstraints constraints) override {}
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) override {
    fuchsia::sysmem::BufferCollectionInfo_2 info{};
    info.buffer_count = 1;
    info.settings.has_image_format_constraints = true;
    info.settings.image_format_constraints.pixel_format.type = kDmabufSysmemFormat;
    info.settings.image_format_constraints.pixel_format.has_format_modifier = true;
    info.settings.image_format_constraints.pixel_format.format_modifier.value =
        fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
    info.settings.image_format_constraints.min_coded_width = kDmabufWidth;
    info.settings.image_format_constraints.min_coded_height = kDmabufHeight;
    info.settings.image_format_constraints.max_coded_width = kDmabufWidth;
    info.settings.image_format_constraints.max_coded_height = kDmabufHeight;
    info.settings.image_format_constraints.min_bytes_per_row = kDmabufWidth * 4;
    info.settings.image_format_constraints.bytes_per_row_divisor = 1;
    zx::vmo::create(kDmabufWidth * kDmabufHeight * 4, 0, &info.buffers[0].vmo);
    callback(ZX_OK, std::move(info));
  }

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented BufferCollection." << name;
  }

  fidl::Binding<fuchsia::sysmem::BufferCollection> binding_;
};

class TestSysmemAllocator : public fuchsia::sysmem::testing::Allocator_TestBase {
 public:
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> Bind(async_dispatcher_t* dispatcher) {
    return binding_.NewBinding(dispatcher);
  }

 private:
  // |fuchsia::sysmem::Allocator|
  void AllocateSharedCollection(
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> request) override {
    tokens_.emplace_back(std::make_unique<TestBufferCollectionToken>(std::move(request)));
  }
  void BindSharedCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) override {
    collections_.emplace_back(std::make_unique<TestBufferCollection>(std::move(request)));
  }
  void SetDebugClientInfo(std::string name, uint64_t id) override {}

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented Allocator." << name;
  }

  std::vector<std::unique_ptr<TestBufferCollectionToken>> tokens_;
  std::vector<std::unique_ptr<TestBufferCollection>> collections_;
  fidl::Binding<fuchsia::sysmem::Allocator> binding_{this};
};

class TestAllocator : public fuchsia::ui::composition::testing::Allocator_TestBase {
 public:
  fidl::InterfaceHandle<fuchsia::ui::composition::Allocator> Bind(async_dispatcher_t* dispatcher) {
    return binding_.NewBinding(dispatcher);
  }

 private:
  void RegisterBufferCollection(fuchsia::ui::composition::RegisterBufferCollectionArgs args,
                                RegisterBufferCollectionCallback callback) override {
    callback(fpromise::ok());
  }

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented Allocator." << name;
  }

  fidl::Binding<fuchsia::ui::composition::Allocator> binding_{this};
};

class VirtioWlTest : public TestWithDevice {
 public:
  VirtioWlTest()
      : wl_dispatcher_([this](zx::channel channel) { channels_.emplace_back(std::move(channel)); }),
        in_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        out_queue_(phys_mem_, in_queue_.end(), kQueueSize),
        sysmem_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        scenic_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    uintptr_t vmar_addr;
    zx::vmar vmar;
    ASSERT_EQ(
        zx::vmar::root_self()->allocate(kAllocateFlags, 0u, kVirtioWlVmarSize, &vmar, &vmar_addr),
        ZX_OK);

    constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_wl#meta/virtio_wl.cm";
    constexpr auto kComponentName = "virtio_wl";

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kComponentUrl);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioWayland::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    wl_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioWayland>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(out_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    ASSERT_EQ(sysmem_loop_.StartThread(), ZX_OK);
    ASSERT_EQ(scenic_loop_.StartThread(), ZX_OK);

    status = wl_->Start(std::move(start_info), std::move(vmar), wl_dispatcher_.Bind(),
                        sysmem_allocator_.Bind(sysmem_loop_.dispatcher()),
                        scenic_allocator_.Bind(scenic_loop_.dispatcher()));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&in_queue_, &out_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = wl_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }

    // Finish negotiating features.
    status = wl_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
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
  TestSysmemAllocator sysmem_allocator_;
  TestAllocator scenic_allocator_;
  fuchsia::virtualization::hardware::VirtioWaylandSyncPtr wl_;
  VirtioQueueFake in_queue_;
  VirtioQueueFake out_queue_;
  std::vector<zx::channel> channels_;
  async::Loop sysmem_loop_;
  async::Loop scenic_loop_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
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
  request.dmabuf.format = kDmabufDrmFormat;
  request.dmabuf.width = kDmabufWidth;
  request.dmabuf.height = kDmabufHeight;
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

  virtio_wl_ctrl_vfd_dmabuf_sync_t sync_request = {};
  sync_request.hdr.type = VIRTIO_WL_CMD_VFD_DMABUF_SYNC;
  sync_request.vfd_id = 1u;
  virtio_wl_ctrl_hdr_t* sync_response;
  ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                .AppendReadableDescriptor(&sync_request, sizeof(sync_request))
                .AppendWritableDescriptor(&sync_response, sizeof(*sync_response))
                .Build(&descriptor_id),
            ZX_OK);

  ASSERT_EQ(wl_->NotifyQueue(VIRTWL_VQ_OUT), ZX_OK);
  used_elem = NextUsed(&out_queue_);
  EXPECT_TRUE(used_elem);
  EXPECT_EQ(used_elem->id, descriptor_id);
  EXPECT_EQ(used_elem->len, sizeof(*sync_response));
  EXPECT_EQ(sync_response->type, VIRTIO_WL_RESP_OK);
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
                .AppendWritableDescriptor(&buffer, static_cast<uint32_t>(buffer_size))
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
                .AppendWritableDescriptor(&buffer, static_cast<uint32_t>(buffer_size))
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

TEST_F(VirtioWlTest, ImportExportImage) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);
  fuchsia::virtualization::hardware::VirtioWaylandImporterSyncPtr importer;
  ASSERT_EQ(wl_->GetImporter(importer.NewRequest()), ZX_OK);

  virtio_wl_ctrl_vfd_new_t* new_vfd_cmd;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&new_vfd_cmd, sizeof(virtio_wl_ctrl_vfd_new_t))
                .Build(&descriptor_id),
            ZX_OK);

  uint32_t vfd_id = 0;
  uint64_t koid = 0;
  {
    fuchsia::virtualization::hardware::VirtioImage image;
    ASSERT_EQ(zx::vmo::create(kImportVmoSize, 0u, &image.vmo), ZX_OK);
    zx_info_handle_basic_t info;
    ASSERT_EQ(image.vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    koid = info.koid;
    ASSERT_EQ(importer->ImportImage(std::move(image), &vfd_id), ZX_OK);
    ASSERT_NE(vfd_id, 0u);
  }
  {
    std::unique_ptr<fuchsia::virtualization::hardware::VirtioImage> image;
    zx_status_t result;
    ASSERT_EQ(importer->ExportImage(vfd_id, &result, &image), ZX_OK);
    ASSERT_TRUE(image);
    EXPECT_EQ(result, ZX_OK);
    zx_info_handle_basic_t info;
    ASSERT_EQ(image->vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    EXPECT_EQ(koid, info.koid);
  }
}

TEST_F(VirtioWlTest, ImportExportImageWithToken) {
  ASSERT_EQ(CreateConnection(1u), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(channels_.size(), 1u);
  fuchsia::virtualization::hardware::VirtioWaylandImporterSyncPtr importer;
  ASSERT_EQ(wl_->GetImporter(importer.NewRequest()), ZX_OK);

  virtio_wl_ctrl_vfd_new_t* new_vfd_cmd;
  uint16_t descriptor_id;
  ASSERT_EQ(DescriptorChainBuilder(in_queue_)
                .AppendWritableDescriptor(&new_vfd_cmd, sizeof(virtio_wl_ctrl_vfd_new_t))
                .Build(&descriptor_id),
            ZX_OK);

  uint32_t vfd_id = 0;
  uint64_t koid = 0;
  {
    fuchsia::virtualization::hardware::VirtioImage image;
    ASSERT_EQ(zx::vmo::create(kImportVmoSize, 0u, &image.vmo), ZX_OK);
    zx_info_handle_basic_t info;
    ASSERT_EQ(image.vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    koid = info.koid;
    zx::eventpair ep;
    ASSERT_EQ(zx::eventpair::create(0, &ep, &image.token), ZX_OK);
    ASSERT_EQ(importer->ImportImage(std::move(image), &vfd_id), ZX_OK);
    ASSERT_NE(vfd_id, 0u);
  }
  {
    std::unique_ptr<fuchsia::virtualization::hardware::VirtioImage> image;
    zx_status_t result;
    ASSERT_EQ(importer->ExportImage(vfd_id, &result, &image), ZX_OK);
    ASSERT_TRUE(image);
    EXPECT_EQ(result, ZX_OK);
    zx_info_handle_basic_t info;
    ASSERT_EQ(image->vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    EXPECT_EQ(koid, info.koid);
    ASSERT_EQ(image->token.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENTPAIR);
  }
}

}  // namespace
