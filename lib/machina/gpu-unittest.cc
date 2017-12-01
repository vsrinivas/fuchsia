// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu.h"

#include <fbl/unique_ptr.h>

#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

static constexpr uint32_t kDisplayWidth = 1024;
static constexpr uint32_t kDisplayHeight = 768;
static constexpr uint32_t kPixelFormat = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
static constexpr uint16_t kQueueSize = 32;
static constexpr uint32_t kRootResourceId = 1;
static constexpr uint32_t kScanoutId = 0;

struct BackingPages
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<BackingPages>> {
  BackingPages(size_t size) : buffer(new uint8_t[size]), len(size) {}

  fbl::unique_ptr<uint8_t[]> buffer;
  size_t len;
};

class VirtioGpuTest {
 public:
  VirtioGpuTest()
      : gpu_(0, UINTPTR_MAX), control_queue_(&gpu_.control_queue()) {}

  zx_status_t Init() {
    zx_status_t status = control_queue_.Init(kQueueSize);
    if (status != ZX_OK)
      return status;
    return CreateScanout(kDisplayWidth, kDisplayHeight);
  }

  VirtioGpu& gpu() { return gpu_; }

  VirtioQueueFake& control_queue() { return control_queue_; }

  uint8_t* scanout_buffer() const { return scanout_buffer_; }

  size_t scanout_size() const { return scanout_size_; }

  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>>& backing_pages() {
    return backing_pages_;
  }

  zx_status_t CreateScanout(uint32_t width, uint32_t height) {
    GpuBitmap surface(width, height);
    scanout_size_ = width * height * VirtioGpu::kBytesPerPixel;
    scanout_buffer_ = surface.buffer();

    auto scanout = fbl::make_unique<GpuScanout>(fbl::move(surface));
    gpu_.AddScanout(fbl::move(scanout));
    return ZX_OK;
  }

  zx_status_t CreateRootResource() {
    virtio_gpu_resource_create_2d_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    request.format = kPixelFormat;
    request.resource_id = kRootResourceId;
    request.width = kDisplayWidth;
    request.height = kDisplayHeight;

    uint16_t desc = 0;
    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWriteable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK)
      return status;

    uint32_t used = 0;
    status = gpu_.HandleGpuCommand(&gpu_.control_queue(), desc, &used);
    EXPECT_EQ(sizeof(response), used);
    if (status != ZX_OK)
      return status;

    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

  // Attaches a single, contiguous memory region.
  zx_status_t AttachBacking() {
    virtio_gpu_resource_attach_backing_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    request.resource_id = kRootResourceId;
    request.nr_entries = 1;

    uint32_t size = kDisplayWidth * kDisplayHeight * VirtioGpu::kBytesPerPixel;
    auto backing = fbl::make_unique<BackingPages>(size);
    virtio_gpu_mem_entry_t entry = {};
    entry.addr = reinterpret_cast<uint64_t>(backing->buffer.get());
    entry.length = size;
    backing_pages_.push_front(fbl::move(backing));

    uint16_t desc = 0;
    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendReadable(&entry, sizeof(entry))
                             .AppendWriteable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK)
      return status;

    uint32_t used = 0;
    status = gpu_.HandleGpuCommand(&gpu_.control_queue(), desc, &used);
    EXPECT_EQ(sizeof(response), used);
    if (status != ZX_OK)
      return status;

    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

  zx_status_t SetScanout() {
    virtio_gpu_set_scanout_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    request.resource_id = kRootResourceId;
    request.scanout_id = kScanoutId;
    request.r.x = 0;
    request.r.y = 0;
    request.r.width = kDisplayWidth;
    request.r.height = kDisplayHeight;

    uint16_t desc = 0;
    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWriteable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK)
      return status;

    uint32_t used = 0;
    status = gpu_.HandleGpuCommand(&gpu_.control_queue(), desc, &used);
    EXPECT_EQ(sizeof(response), used);
    if (status != ZX_OK)
      return status;

    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

  zx_status_t Flush() {
    virtio_gpu_resource_flush request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    request.resource_id = kRootResourceId;
    request.r.x = 0;
    request.r.y = 0;
    request.r.width = kDisplayWidth;
    request.r.height = kDisplayHeight;

    uint16_t desc = 0;
    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWriteable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK)
      return status;

    uint32_t used = 0;
    status = gpu_.HandleGpuCommand(&gpu_.control_queue(), desc, &used);
    EXPECT_EQ(sizeof(response), used);
    if (status != ZX_OK)
      return status;

    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

 private:
  VirtioGpu gpu_;
  VirtioQueueFake control_queue_;
  // Backing pages for the root resource.
  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>> backing_pages_;

  // A direct pointer into our scanout buffer.
  uint8_t* scanout_buffer_ = nullptr;
  size_t scanout_size_ = 0;
};

TEST(VirtioGpuTest, HandleGetDisplayInfo) {
  uint16_t desc;
  VirtioGpuTest test;
  ASSERT_EQ(test.Init(), ZX_OK);

  virtio_gpu_ctrl_hdr_t request = {};
  virtio_gpu_resp_display_info_t response = {};
  ASSERT_EQ(test.control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWriteable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);

  uint32_t used = 0;
  request.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  ASSERT_EQ(
      test.gpu().HandleGpuCommand(&test.gpu().control_queue(), desc, &used),
      ZX_OK);

  EXPECT_EQ(sizeof(response), used);
  EXPECT_EQ(response.hdr.type, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
  EXPECT_EQ(response.pmodes[0].r.x, 0u);
  EXPECT_EQ(response.pmodes[0].r.y, 0u);
  EXPECT_EQ(response.pmodes[0].r.width, kDisplayWidth);
  EXPECT_EQ(response.pmodes[0].r.height, kDisplayHeight);
}

// Test the basic device initialization sequence.
TEST(VirtioGpuTest, HandleInitialization) {
  VirtioGpuTest test;
  ASSERT_EQ(test.Init(), ZX_OK);

  ASSERT_EQ(test.CreateRootResource(), ZX_OK);
  ASSERT_EQ(test.AttachBacking(), ZX_OK);
  ASSERT_EQ(test.SetScanout(), ZX_OK);
}

TEST(VirtioGpuTest, SetScanoutToInvalidResource) {
  VirtioGpuTest test;
  ASSERT_EQ(test.Init(), ZX_OK);

  ASSERT_EQ(test.CreateRootResource(), ZX_OK);
  ASSERT_EQ(test.AttachBacking(), ZX_OK);

  virtio_gpu_set_scanout_t request = {};
  request.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  request.resource_id = 123;
  request.scanout_id = kScanoutId;
  request.r.x = 0;
  request.r.y = 0;
  request.r.width = kDisplayWidth;
  request.r.height = kDisplayHeight;

  uint16_t desc = 0;
  virtio_gpu_ctrl_hdr_t response = {};
  ASSERT_EQ(test.control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWriteable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);

  uint32_t used = 0;
  ASSERT_EQ(
      test.gpu().HandleGpuCommand(&test.gpu().control_queue(), desc, &used),
      ZX_OK);
  EXPECT_EQ(sizeof(response), used);
  ASSERT_EQ(response.type, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
}

// Verify a basic transfer 2d command correctly fills in the scanout.
TEST(VirtioGpuTest, HandleTransfer2D) {
  VirtioGpuTest test;
  ASSERT_EQ(test.Init(), ZX_OK);

  ASSERT_EQ(test.CreateRootResource(), ZX_OK);
  ASSERT_EQ(test.AttachBacking(), ZX_OK);
  ASSERT_EQ(test.SetScanout(), ZX_OK);

  virtio_gpu_transfer_to_host_2d_t request = {};
  request.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  request.resource_id = kRootResourceId;
  request.r.x = 0;
  request.r.y = 0;
  request.r.width = kDisplayWidth;
  request.r.height = kDisplayHeight;

  uint16_t desc = 0;
  virtio_gpu_ctrl_hdr_t response = {};
  ASSERT_EQ(test.control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWriteable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);

  // Initialize the scanout to 0x00 and write 0xff to the backing pages.
  // A transfer 2d command will copy the 0xff into the scanout buffer.
  memset(test.scanout_buffer(), 0, test.scanout_size());
  for (const auto& entry : test.backing_pages()) {
    memset(entry.buffer.get(), 0xff, entry.len);
  }

  uint32_t used = 0;
  ASSERT_EQ(
      test.gpu().HandleGpuCommand(&test.gpu().control_queue(), desc, &used),
      ZX_OK);
  EXPECT_EQ(sizeof(response), used);
  ASSERT_EQ(response.type, VIRTIO_GPU_RESP_OK_NODATA);

  // Send a flush command to draw bytes to our scanout buffer.
  ASSERT_EQ(test.Flush(), ZX_OK);

  // Verify backing/scanout are now in sync.
  size_t offset = 0;
  for (const auto& entry : test.backing_pages()) {
    ASSERT_EQ(
        memcmp(entry.buffer.get(), test.scanout_buffer() + offset, entry.len),
        0);
    offset += entry.len;
  }
}
