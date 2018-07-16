// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_gpu.h"

#include <fbl/unique_ptr.h>

#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "lib/gtest/test_loop_fixture.h"

namespace machina {
namespace {

static constexpr uint32_t kDisplayWidth = 1024;
static constexpr uint32_t kDisplayHeight = 768;
static constexpr uint32_t kCursorWidth = 16;
static constexpr uint32_t kCursorHeight = 16;
static constexpr uint32_t kPixelFormat = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
static constexpr uint8_t kPixelSize = 4;
static constexpr uint16_t kQueueSize = 32;
static constexpr uint32_t kRootResourceId = 1;
static constexpr uint32_t kCursorResourceId = 2;
static constexpr uint32_t kScanoutId = 0;

struct BackingPages
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<BackingPages>> {
  BackingPages(size_t size) : buffer(new uint8_t[size]), len(size) {}

  fbl::unique_ptr<uint8_t[]> buffer;
  size_t len;
};

class VirtioGpuTest : public ::gtest::TestLoopFixture {
 public:
  VirtioGpuTest()
      : gpu_(phys_mem_, dispatcher()), control_queue_(gpu_.control_queue()) {}

  zx_status_t Init() {
    zx_status_t status = control_queue_.Init(kQueueSize);
    if (status != ZX_OK) {
      return status;
    }
    status = gpu_.Init();
    if (status != ZX_OK) {
      return status;
    }
    return CreateScanout(kDisplayWidth, kDisplayHeight);
  }

  VirtioGpu& gpu() { return gpu_; }

  VirtioQueueFake& control_queue() { return control_queue_; }

  uint8_t* scanout_buffer() const { return scanout_buffer_; }

  size_t scanout_size() const { return scanout_size_; }

  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>>& root_backing_pages() {
    return root_backing_pages_;
  }

  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>>& cursor_backing_pages() {
    return cursor_backing_pages_;
  }

  zx_status_t CreateScanout(uint32_t width, uint32_t height) {
    GpuBitmap surface(width, height, ZX_PIXEL_FORMAT_ARGB_8888);
    scanout_size_ = width * height * surface.pixelsize();
    scanout_buffer_ = surface.buffer();

    scanout_.SetBitmap(std::move(surface));
    gpu_.AddScanout(&scanout_);
    return ZX_OK;
  }

  zx_status_t CreateResource(uint32_t resource_id, uint32_t width,
                             uint32_t height) {
    virtio_gpu_resource_create_2d_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    request.format = kPixelFormat;
    request.resource_id = resource_id;
    request.width = width;
    request.height = height;

    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendWritable(&response, sizeof(response))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(control_queue_.HasUsed());
    EXPECT_EQ(sizeof(response), control_queue_.NextUsed().len);
    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

  zx_status_t CreateRootResource() {
    return CreateResource(kRootResourceId, kDisplayWidth, kDisplayHeight);
  }

  zx_status_t CreateCursorResource() {
    return CreateResource(kCursorResourceId, kCursorWidth, kCursorHeight);
  }

  zx_status_t AttachRootBacking() {
    return AttachBacking(kRootResourceId, kDisplayWidth, kDisplayHeight,
                         &root_backing_pages_);
  }

  zx_status_t AttachCursorBacking() {
    return AttachBacking(kCursorResourceId, kCursorWidth, kCursorHeight,
                         &cursor_backing_pages_);
  }

  // Attaches a single, contiguous memory region to the resource.
  zx_status_t AttachBacking(
      uint32_t resource_id, uint32_t width, uint32_t height,
      fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>>* backing_pages) {
    virtio_gpu_resource_attach_backing_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    request.resource_id = resource_id;
    request.nr_entries = 1;

    uint32_t size = width * height * kPixelSize;
    auto backing = fbl::make_unique<BackingPages>(size);
    virtio_gpu_mem_entry_t entry = {};
    entry.addr = reinterpret_cast<uint64_t>(backing->buffer.get());
    entry.length = size;
    backing_pages->push_front(std::move(backing));

    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status = control_queue()
                             .BuildDescriptor()
                             .AppendReadable(&request, sizeof(request))
                             .AppendReadable(&entry, sizeof(entry))
                             .AppendWritable(&response, sizeof(response))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(control_queue_.HasUsed());
    EXPECT_EQ(sizeof(response), control_queue_.NextUsed().len);
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
                             .AppendWritable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(control_queue_.HasUsed());
    EXPECT_EQ(sizeof(response), control_queue_.NextUsed().len);
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
                             .AppendWritable(&response, sizeof(response))
                             .Build(&desc);
    if (status != ZX_OK) {
      return status;
    }

    RunLoopUntilIdle();
    EXPECT_TRUE(control_queue_.HasUsed());
    EXPECT_EQ(sizeof(response), control_queue_.NextUsed().len);
    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? ZX_OK : response.type;
  }

 private:
  PhysMemFake phys_mem_;
  VirtioGpu gpu_;
  GpuScanout scanout_;
  VirtioQueueFake control_queue_;
  // Backing pages for the root resource.
  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>> root_backing_pages_;
  fbl::SinglyLinkedList<fbl::unique_ptr<BackingPages>> cursor_backing_pages_;

  // A direct pointer into our scanout buffer.
  uint8_t* scanout_buffer_ = nullptr;
  size_t scanout_size_ = 0;
};

TEST_F(VirtioGpuTest, HandleGetDisplayInfo) {
  ASSERT_EQ(Init(), ZX_OK);

  virtio_gpu_ctrl_hdr_t request = {};
  request.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  virtio_gpu_resp_display_info_t response = {};
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWritable(&response, sizeof(response))
                .Build(),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(sizeof(response), control_queue().NextUsed().len);
  EXPECT_EQ(response.hdr.type, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
  EXPECT_EQ(response.pmodes[0].r.x, 0u);
  EXPECT_EQ(response.pmodes[0].r.y, 0u);
  EXPECT_EQ(response.pmodes[0].r.width, kDisplayWidth);
  EXPECT_EQ(response.pmodes[0].r.height, kDisplayHeight);
}

// Test the basic device initialization sequence.
TEST_F(VirtioGpuTest, HandleInitialization) {
  ASSERT_EQ(Init(), ZX_OK);

  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);
}

TEST_F(VirtioGpuTest, SetScanoutToInvalidResource) {
  ASSERT_EQ(Init(), ZX_OK);

  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);

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
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWritable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(sizeof(response), control_queue().NextUsed().len);
  ASSERT_EQ(response.type, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
}

// Verify a basic transfer 2d command correctly fills in the scanout.
TEST_F(VirtioGpuTest, HandleTransfer2D) {
  ASSERT_EQ(Init(), ZX_OK);

  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);

  virtio_gpu_transfer_to_host_2d_t request = {};
  request.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  request.resource_id = kRootResourceId;
  request.r.x = 0;
  request.r.y = 0;
  request.r.width = kDisplayWidth;
  request.r.height = kDisplayHeight;

  uint16_t desc = 0;
  virtio_gpu_ctrl_hdr_t response = {};
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .AppendWritable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);

  // Initialize the scanout to 0x00 and write 0xff to the backing pages.
  // A transfer 2d command will copy the 0xff into the scanout buffer.
  memset(scanout_buffer(), 0, scanout_size());
  for (const auto& entry : root_backing_pages()) {
    memset(entry.buffer.get(), 0xff, entry.len);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(sizeof(response), control_queue().NextUsed().len);
  ASSERT_EQ(response.type, VIRTIO_GPU_RESP_OK_NODATA);

  // Send a flush command to draw bytes to our scanout buffer.
  ASSERT_EQ(Flush(), ZX_OK);

  // Verify backing/scanout are now in sync.
  size_t offset = 0;
  for (const auto& entry : root_backing_pages()) {
    ASSERT_EQ(memcmp(entry.buffer.get(), scanout_buffer() + offset, entry.len),
              0);
    offset += entry.len;
  }
}

TEST_F(VirtioGpuTest, DrawCursor) {
  ASSERT_EQ(Init(), ZX_OK);

  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);
  ASSERT_EQ(CreateCursorResource(), ZX_OK);
  ASSERT_EQ(AttachCursorBacking(), ZX_OK);

  // Initialize the scanout to 0xab and the cursor resource to 0xff.
  memset(scanout_buffer(), 0xab, scanout_size());
  for (const auto& entry : cursor_backing_pages()) {
    memset(entry.buffer.get(), 0xff, entry.len);
  }
  virtio_gpu_transfer_to_host_2d_t transfer_request = {};
  transfer_request.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  transfer_request.resource_id = kCursorResourceId;
  transfer_request.r.x = 0;
  transfer_request.r.y = 0;
  transfer_request.r.width = kCursorWidth;
  transfer_request.r.height = kCursorHeight;
  uint16_t desc = 0;
  virtio_gpu_ctrl_hdr_t response = {};
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&transfer_request, sizeof(transfer_request))
                .AppendWritable(&response, sizeof(response))
                .Build(&desc),
            ZX_OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(sizeof(response), control_queue().NextUsed().len);

  // Update cursor.
  const uint32_t cursor_pos_x = 64;
  const uint32_t cursor_pos_y = 64;
  virtio_gpu_update_cursor_t request = {};
  request.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
  request.resource_id = kCursorResourceId;
  request.pos.x = cursor_pos_x;
  request.pos.y = cursor_pos_y;
  request.pos.scanout_id = kScanoutId;
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .Build(&desc),
            ZX_OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(0u, control_queue().NextUsed().len);

  // Verify cursor is drawn to the scanout.
  uint32_t* scanout = reinterpret_cast<uint32_t*>(scanout_buffer());
  for (size_t y = 0; y < kDisplayHeight; ++y) {
    for (size_t x = 0; x < kDisplayWidth; ++x) {
      size_t index = y * kDisplayWidth + x;
      if (y >= cursor_pos_y && y < cursor_pos_y + kCursorHeight &&
          x >= cursor_pos_x && x < cursor_pos_x + kCursorWidth) {
        ASSERT_EQ(0xffffffff, scanout[index]);
      } else {
        ASSERT_EQ(0xabababab, scanout[index]);
      }
    }
  }

  // Clear cursor.
  request.resource_id = 0;
  ASSERT_EQ(control_queue()
                .BuildDescriptor()
                .AppendReadable(&request, sizeof(request))
                .Build(&desc),
            ZX_OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(0u, control_queue().NextUsed().len);
}

}  // namespace
}  // namespace machina
