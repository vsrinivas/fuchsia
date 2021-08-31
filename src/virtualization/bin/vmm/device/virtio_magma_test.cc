// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"

#include <drm_fourcc.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/socket.h>
#include <string.h>

#include <fbl/algorithm.h>

#include "src/graphics/lib/magma/include/magma_abi/magma.h"
#include "src/lib/fsl/handles/object_info.h"
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
  using VirtioImage = fuchsia::virtualization::hardware::VirtioImage;

  void ImportImage(VirtioImage image, ImportImageCallback callback) override {
    EXPECT_EQ(
        zx_object_get_info(image.vmo.get(), ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr),
        ZX_OK);
    zx_info_handle_basic_t handle_info{};
    EXPECT_EQ(zx_object_get_info(image.vmo.get(), ZX_INFO_HANDLE_BASIC, &handle_info,
                                 sizeof(handle_info), nullptr, nullptr),
              ZX_OK);
    EXPECT_EQ(handle_info.type, ZX_OBJ_TYPE_VMO);
    image_ = std::make_unique<VirtioImage>();
    std::swap(*image_, image);
    callback(kMockVfdId);
  }
  void ExportImage(uint32_t vfd_id, ExportImageCallback callback) override {
    EXPECT_EQ(kMockVfdId, vfd_id);
    if (vfd_id == kMockVfdId) {
      callback(ZX_OK, std::move(image_));
    } else {
      callback(ZX_ERR_NOT_FOUND, {});
    }
  }

  std::unique_ptr<VirtioImage> image_;
};

class ScenicAllocatorFake : public fuchsia::ui::composition::Allocator {
 public:
  // Must set constraints on the given buffer collection token to allow the constraints
  // negotiation to complete.
  void RegisterBufferCollection(fuchsia::ui::composition::RegisterBufferCollectionArgs args,
                                RegisterBufferCollectionCallback callback) override {
    if (!args.has_export_token()) {
      FX_LOGS(ERROR) << "RegisterBufferCollection called with missing export token";
      callback(
          fpromise::error(fuchsia::ui::composition::RegisterBufferCollectionError::BAD_OPERATION));
      return;
    }

    if (!args.has_buffer_collection_token()) {
      FX_LOGS(ERROR) << "RegisterBufferCollection called with missing buffer collection token";
      callback(
          fpromise::error(fuchsia::ui::composition::RegisterBufferCollectionError::BAD_OPERATION));
      return;
    }

    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(sysmem_allocator.NewRequest());
    sysmem_allocator->SetDebugClientInfo(fsl::GetCurrentProcessName(),
                                         fsl::GetCurrentProcessKoid());

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    zx_status_t status = sysmem_allocator->BindSharedCollection(
        std::move(*args.mutable_buffer_collection_token()), buffer_collection.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "BindSharedCollection failed: " << status;
      callback(
          fpromise::error(fuchsia::ui::composition::RegisterBufferCollectionError::BAD_OPERATION));
      return;
    }

    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.min_buffer_count = 1;
    constraints.usage.cpu =
        fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
    constraints.has_buffer_memory_constraints = true;
    constraints.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    image_constraints = fuchsia::sysmem::ImageFormatConstraints();
    image_constraints.min_coded_width = 0;
    image_constraints.min_coded_height = 0;
    image_constraints.max_coded_width = 0;
    image_constraints.max_coded_height = 0;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
    image_constraints.pixel_format.has_format_modifier = false;

    status = buffer_collection->SetConstraints(true, constraints);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "SetConstraints failed: " << status;
      callback(
          fpromise::error(fuchsia::ui::composition::RegisterBufferCollectionError::BAD_OPERATION));
      return;
    }

    buffer_collection->Close();

    callback(fpromise::ok());
  }
};

//////////////////////////////////////////////////////////////////////////////////////////////////

class VirtioMagmaTest : public TestWithDevice {
 public:
  VirtioMagmaTest()
      : out_queue_(phys_mem_, kDescriptorSize, kQueueSize),
        wayland_importer_mock_binding_(&wayland_importer_mock_),
        wayland_importer_mock_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        scenic_allocator_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    uintptr_t vmar_addr;
    zx::vmar vmar;
    ASSERT_EQ(zx::vmar::root_self()->allocate(kAllocateFlags, 0u, kVirtioMagmaVmarSize, &vmar,
                                              &vmar_addr),
              ZX_OK);

    fuchsia::virtualization::hardware::StartInfo start_info;
    {
      std::unique_ptr<sys::testing::EnvironmentServices> env_services = CreateServices();
      env_services->AllowParentService("fuchsia.vulkan.loader.Loader");
      env_services->AllowParentService("fuchsia.sysmem.Allocator");

      ASSERT_EQ(ZX_OK, env_services->AddService(scenic_allocator_binding_set_.GetHandler(
                           &scenic_allocator_fake_, scenic_allocator_loop_.dispatcher())));

      ASSERT_EQ(ZX_OK, LaunchDevice(kVirtioMagmaUrl, out_queue_.end(), &start_info,
                                    std::move(env_services)));
    }

    // Start device execution.
    ASSERT_EQ(wayland_importer_mock_loop_.StartThread(), ZX_OK);
    ASSERT_EQ(scenic_allocator_loop_.StartThread(), ZX_OK);

    services_->Connect(magma_.NewRequest());

    {
      zx_status_t status;
      magma_->Start(
          std::move(start_info), std::move(vmar),
          wayland_importer_mock_binding_.NewBinding(wayland_importer_mock_loop_.dispatcher()),
          [&](zx_status_t start_status) {
            status = start_status;
            QuitLoop();
          });
      RunLoop();
      if (status == ZX_ERR_NOT_FOUND) {
        ADD_FAILURE() << "Failed to start VirtioMagma because no GPU devices were found.";
      }
      ASSERT_EQ(ZX_OK, status);
    }

    // Configure device queues.
    out_queue_.Configure(0, kDescriptorSize);
    magma_->ConfigureQueue(0, out_queue_.size(), out_queue_.desc(), out_queue_.avail(),
                           out_queue_.used(), [&] { QuitLoop(); });
    RunLoop();

    // Finish negotiating features.
    magma_->Ready(0, [&] { QuitLoop(); });
    RunLoop();
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
    magma_->NotifyQueue(0);
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
    magma_->NotifyQueue(0);
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
    magma_->NotifyQueue(0);
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
    magma_->NotifyQueue(0);
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
    magma_->NotifyQueue(0);
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
    magma_->NotifyQueue(0);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_RELEASE_BUFFER);
    EXPECT_EQ(response.hdr.flags, 0u);
  }

  void CreateImage(uint64_t connection, magma_buffer_t* image_out) {
    virtio_magma_virt_create_image_ctrl_t request = {.hdr.type = VIRTIO_MAGMA_CMD_VIRT_CREATE_IMAGE,
                                                     .connection = connection};
    magma_image_create_info_t create_image = {
        .drm_format = DRM_FORMAT_ARGB8888,
        .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
        .width = 1920,
        .height = 1080,
        // Presentable causes VirtioMagma to register buffer collection with scenic
        .flags = MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE,
    };

    std::vector<uint8_t> request_buffer(sizeof(request) + sizeof(create_image));
    memcpy(request_buffer.data(), &request, sizeof(request));
    memcpy(request_buffer.data() + sizeof(request), &create_image, sizeof(create_image));

    virtio_magma_virt_create_image_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(request_buffer.data(),
                                            static_cast<uint32_t>(request_buffer.size()))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);
    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_VIRT_CREATE_IMAGE);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
    EXPECT_NE(response.image_out, 0u);
    *image_out = response.image_out;
  }

 protected:
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioMagmaPtr magma_;
  VirtioQueueFake out_queue_;
  WaylandImporterMock wayland_importer_mock_;
  fidl::Binding<fuchsia::virtualization::hardware::VirtioWaylandImporter>
      wayland_importer_mock_binding_;
  async::Loop wayland_importer_mock_loop_;
  ScenicAllocatorFake scenic_allocator_fake_;
  async::Loop scenic_allocator_loop_;
  fidl::BindingSet<fuchsia::ui::composition::Allocator> scenic_allocator_binding_set_;
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
    magma_->NotifyQueue(0);

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
    magma_->NotifyQueue(0);

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

TEST_F(VirtioMagmaTest, HandleReadNotificationChannel2) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));
  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));

  {
    virtio_magma_read_notification_channel2_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_READ_NOTIFICATION_CHANNEL2;
    constexpr uint32_t kMagicFlags = 0xabcd1234;
    request.hdr.flags =
        kMagicFlags;  // VirtioMagma will put these magic flags in the returned buffer
    request.connection = connection;
    request.buffer_size = sizeof(uint32_t);
    request.buffer = 0;  // not used
    virtio_magma_read_notification_channel2_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(
                      &response_ptr, static_cast<uint32_t>(sizeof(response) + request.buffer_size))
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response) + request.buffer_size);

    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_READ_NOTIFICATION_CHANNEL2);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
    EXPECT_EQ(response.buffer_size_out, sizeof(uint32_t));
    EXPECT_EQ(response.more_data_out, 0u);

    uint32_t buffer;
    memcpy(&buffer,
           reinterpret_cast<virtio_magma_read_notification_channel2_resp_t*>(response_ptr) + 1,
           sizeof(buffer));
    EXPECT_EQ(buffer, kMagicFlags);
  }

  ASSERT_NO_FATAL_FAILURE(ReleaseConnection(connection));
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

TEST_F(VirtioMagmaTest, HandleImportExport) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));
  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));

  magma_buffer_t image{};
  ASSERT_NO_FATAL_FAILURE(CreateImage(connection, &image));

  {
    virtio_magma_export_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_EXPORT;
    request.connection = connection;
    request.buffer = image;
    virtio_magma_export_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);

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
  {
    virtio_magma_import_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_IMPORT;
    request.connection = connection;
    request.buffer_handle = kMockVfdId;
    virtio_magma_import_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));
    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_IMPORT);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_NE(response.buffer_out, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }

  ASSERT_NO_FATAL_FAILURE(ReleaseBuffer(connection, image));
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
    magma_->NotifyQueue(0);

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
    magma_->NotifyQueue(0);

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

TEST_F(VirtioMagmaTest, BufferHandle) {
  magma_device_t device{};
  ASSERT_NO_FATAL_FAILURE(ImportDevice(&device));

  uint64_t connection{};
  ASSERT_NO_FATAL_FAILURE(CreateConnection(device, &connection));

  magma_buffer_t buffer{};
  ASSERT_NO_FATAL_FAILURE(CreateBuffer(connection, &buffer));

  magma_handle_t buffer_handle;
  {
    virtio_magma_get_buffer_handle2_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_GET_BUFFER_HANDLE2;
    request.buffer = buffer;
    uint16_t descriptor_id{};
    virtio_magma_get_buffer_handle2_resp_t response{};
    void* response_ptr;
    constexpr uint64_t kSizeofResponse = sizeof(response) + sizeof(uint64_t);
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, kSizeofResponse)
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, kSizeofResponse);

    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_GET_BUFFER_HANDLE2);
    EXPECT_EQ(response.hdr.flags, 0u);
    EXPECT_NE(response.handle_out, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);

    uint64_t buffer_size;
    memcpy(&buffer_size, reinterpret_cast<uint8_t*>(response_ptr) + sizeof(response),
           sizeof(buffer_size));
    EXPECT_EQ(buffer_size, kBufferSize);

    // This is a copy of the handle bits, not a true handle, so it can only be used as a reference.
    buffer_handle = static_cast<magma_handle_t>(response.handle_out);
  }

  // Releasing the buffer has no effect because VirtioMagma maintains a copy of the handle.
  ASSERT_NO_FATAL_FAILURE(ReleaseBuffer(connection, buffer));

  {
    virtio_magma_internal_release_handle_ctrl_t request{};
    request.hdr.type = VIRTIO_MAGMA_CMD_INTERNAL_RELEASE_HANDLE;
    request.handle = buffer_handle;
    virtio_magma_internal_release_handle_resp_t response{};
    uint16_t descriptor_id{};
    void* response_ptr;
    ASSERT_EQ(DescriptorChainBuilder(out_queue_)
                  .AppendReadableDescriptor(&request, sizeof(request))
                  .AppendWritableDescriptor(&response_ptr, sizeof(response))
                  .Build(&descriptor_id),
              ZX_OK);
    magma_->NotifyQueue(0);

    auto used_elem = NextUsed(&out_queue_);
    EXPECT_TRUE(used_elem);
    EXPECT_EQ(used_elem->id, descriptor_id);
    EXPECT_EQ(used_elem->len, sizeof(response));

    memcpy(&response, response_ptr, sizeof(response));
    EXPECT_EQ(response.hdr.type, VIRTIO_MAGMA_RESP_INTERNAL_RELEASE_HANDLE);
    EXPECT_EQ(response.hdr.flags, 0u);
    ASSERT_EQ(static_cast<magma_status_t>(response.result_return), MAGMA_STATUS_OK);
  }

  ASSERT_NO_FATAL_FAILURE(ReleaseConnection(connection));
  ASSERT_NO_FATAL_FAILURE(ReleaseDevice(device));
}

}  // namespace
