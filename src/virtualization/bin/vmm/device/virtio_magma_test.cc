// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/zx/socket.h>
#include <string.h>

#include <fbl/algorithm.h>

#include "src/graphics/lib/magma/include/magma_abi/magma.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

namespace {

static constexpr char kVirtioMagmaUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_magma#meta/virtio_magma.cmx";
static constexpr uint16_t kQueueSize = 32;
static constexpr size_t kDescriptorSize = PAGE_SIZE;
static constexpr uint32_t kVirtioMagmaVmarSize = 1 << 16;
static constexpr uint32_t kAllocateFlags = ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;
static const size_t kBufferSize = kVirtioMagmaVmarSize / 4;
static const uint32_t kMockVfdId = 42;

class WaylandImporterMock : public fuchsia::virtualization::hardware::VirtioWaylandImporter {
 public:
  // |fuchsia::virtualization::hardware::VirtioWaylandImporter|
  void Import(zx::vmo vmo, ImportCallback callback) override {
    EXPECT_EQ(zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr),
              ZX_OK);
    zx_info_handle_basic_t handle_info{};
    EXPECT_EQ(zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info),
                                 nullptr, nullptr),
              ZX_OK);
    EXPECT_EQ(handle_info.type, ZX_OBJ_TYPE_VMO);
    callback(kMockVfdId);
  }
};

class VirtioMagmaTest : public TestWithDevice {
 public:
  VirtioMagmaTest()
      : out_queue_(phys_mem_, kDescriptorSize, kQueueSize),
        wayland_importer_mock_binding_(&wayland_importer_mock_),
        wayland_importer_mock_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    uintptr_t vmar_addr;
    zx::vmar vmar;
    ASSERT_EQ(zx::vmar::root_self()->allocate2(kAllocateFlags, 0u, kVirtioMagmaVmarSize, &vmar,
                                               &vmar_addr),
              ZX_OK);
    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = LaunchDevice(kVirtioMagmaUrl, out_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    ASSERT_EQ(wayland_importer_mock_loop_.StartThread(), ZX_OK);
    services_->Connect(magma_.NewRequest());
    magma_->Start(
        std::move(start_info), std::move(vmar),
        wayland_importer_mock_binding_.NewBinding(wayland_importer_mock_loop_.dispatcher()),
        &status);
    if (status == ZX_ERR_NOT_FOUND) {
      ADD_FAILURE() << "Failed to start VirtioMagma because no GPU devices were found.";
    }
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    out_queue_.Configure(0, kDescriptorSize);
    status = magma_->ConfigureQueue(0, out_queue_.size(), out_queue_.desc(), out_queue_.avail(),
                                    out_queue_.used());
    ASSERT_EQ(ZX_OK, status);
  }

  std::optional<VirtioQueueFake::UsedElement> NextUsed(VirtioQueueFake* queue) {
    auto elem = queue->NextUsed();
    while (!elem && WaitOnInterrupt() == ZX_OK) {
      elem = queue->NextUsed();
    }
    return elem;
  }

  void ImportDevice(magma_device_t* device_out) {
    virtio_magma_device_import_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_DEVICE_IMPORT;
    virtio_magma_device_import_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_DEVICE_IMPORT);
    EXPECT_EQ(response.hdr.flags, 0u);
    *device_out = response.device_out;
  }

  void ReleaseDevice(magma_device_t device) {
    virtio_magma_device_release_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_DEVICE_RELEASE;
    request.device = device;
    virtio_magma_device_release_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_DEVICE_RELEASE);
  }

  void CreateConnection(magma_device_t device, uint64_t* connection_out) {
    virtio_magma_create_connection2_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_CREATE_CONNECTION2;
    request.device = device;
    virtio_magma_create_connection2_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_CREATE_CONNECTION2);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_GT(response.connection_out, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
    *connection_out = response.connection_out;
  }

  void ReleaseConnection(uint64_t connection) {
    virtio_magma_release_connection_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_RELEASE_CONNECTION;
    request.connection = connection;
    virtio_magma_release_connection_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_RELEASE_CONNECTION);
    EXPECT_EQ(response.hdr.flags, 0u);
  }

  void CreateBuffer(uint64_t connection, magma_buffer_t* buffer_out) {
    virtio_magma_create_buffer_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_CREATE_BUFFER;
    request.connection = connection;
    request.size = kBufferSize;
    virtio_magma_create_buffer_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_CREATE_BUFFER);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_NE(response.buffer_out, 0u);
    EXPECT_GE(response.size_out, kBufferSize);  // The implementation is free to use a larger size.
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
    *buffer_out = response.buffer_out;
  }

  void ReleaseBuffer(uint64_t connection, magma_buffer_t buffer) {
    virtio_magma_release_buffer_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_RELEASE_BUFFER;
    request.connection = connection;
    request.buffer = buffer;
    virtio_magma_release_buffer_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_RELEASE_BUFFER);
    EXPECT_EQ(response.hdr.flags, 0u);
  }

 protected:
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioMagmaSyncPtr magma_;
  VirtioQueueFake out_queue_;
  WaylandImporterMock wayland_importer_mock_;
  fidl::Binding<fuchsia::virtualization::hardware::VirtioWaylandImporter>
      wayland_importer_mock_binding_;
  async::Loop wayland_importer_mock_loop_;
};

TEST_F(VirtioMagmaTest, HandleQuery) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));
  {
    virtio_magma_query2_ctrl_t request{};
    virtio_magma_query2_resp_t response{};
    request.hdr.type = VIRTIO_MAGMA_CMD_QUERY2;
    request.device = device;
    request.id = MAGMA_QUERY_DEVICE_ID;
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_QUERY2);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_GT(response.value_out, 0u);
    EXPECT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

TEST_F(VirtioMagmaTest, HandleConnectionMethod) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));
  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));
  {  // Try to call a method on the connection
    virtio_magma_get_error_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_GET_ERROR;
    request.connection = connection;
    virtio_magma_get_error_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_GET_ERROR);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }
  ASSERT_NO_FATAL_FAILURE(ReleaseConnection(connection));
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

TEST_F(VirtioMagmaTest, HandleExport) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));
  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));

  magma_buffer_t buffer{};
  ASSERT_NO_FATAL_FAILURE(CreateBuffer(connection, &buffer));

  {  // Export the buffer
    virtio_magma_export_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_EXPORT;
    request.connection = connection;
    request.buffer = buffer;
    virtio_magma_export_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_EXPORT);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_EQ(response.buffer_handle_out, kMockVfdId);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }

  ASSERT_NO_FATAL_FAILURE(ReleaseBuffer(connection, buffer));
  ASSERT_NO_FATAL_FAILURE(ReleaseConnection(connection));
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

TEST_F(VirtioMagmaTest, InternalMapAndUnmap) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));

  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));

  magma_buffer_t buffer{};
  ASSERT_NO_FATAL_FAILURE(CreateBuffer(connection, &buffer));

  std::array<uintptr_t, 2> map_lengths = {kBufferSize, kBufferSize};
  std::array<uintptr_t, 2> addr;

  for (size_t i = 0; i < map_lengths.size(); i++) {
    virtio_magma_internal_map_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_INTERNAL_MAP;
    request.connection = connection;
    request.buffer = buffer;
    request.length = map_lengths[i];
    virtio_magma_internal_map_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_INTERNAL_MAP);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_NE(response.address_out, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
    addr[i] = response.address_out;
  }
  for (size_t i = 0; i < map_lengths.size(); i++) {
    virtio_magma_internal_unmap_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_INTERNAL_UNMAP;
    request.connection = connection;
    request.buffer = buffer;
    request.address = addr[i];
    virtio_magma_internal_unmap_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    ASSERT_EQ(magma_->NotifyQueue(0), ZX_OK);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_INTERNAL_UNMAP);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }

  ASSERT_NO_FATAL_FAILURE(ReleaseBuffer(connection, buffer));
  ASSERT_NO_FATAL_FAILURE(ReleaseConnection(connection));
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

}  // namespace
