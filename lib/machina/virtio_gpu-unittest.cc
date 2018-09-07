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
static constexpr uint32_t kCursorWidth = 64;
static constexpr uint32_t kCursorHeight = 64;
static constexpr uint32_t kPixelFormat = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
static constexpr uint8_t kPixelSize = 4;
static constexpr uint16_t kVirtioGpuQueueSize = 32;
static constexpr uint32_t kRootResourceId = 1;
static constexpr uint32_t kCursorResourceId = 2;
static constexpr uint32_t kScanoutId = 0;

struct BackingPages {
  BackingPages(size_t size) : buffer(new uint8_t[size]), len(size) {}

  std::unique_ptr<uint8_t[]> buffer;
  size_t len;
};

class VirtioGpuTest : public ::gtest::TestLoopFixture {
 public:
  VirtioGpuTest()
      : gpu_(phys_mem_, dispatcher()),
        control_queue_(gpu_.control_queue(), kVirtioGpuQueueSize) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, gpu_.Init());
    ASSERT_EQ(ZX_OK, CreateScanout(kDisplayWidth, kDisplayHeight));
  }

  VirtioGpu& gpu() { return gpu_; }

  VirtioQueueFake& control_queue() { return control_queue_; }

  uint8_t* scanout_buffer() const { return scanout_buffer_; }

  size_t scanout_size() const { return scanout_size_; }

  std::vector<std::unique_ptr<BackingPages>>& root_backing_pages() {
    return root_backing_pages_;
  }

  std::vector<std::unique_ptr<BackingPages>>& cursor_backing_pages() {
    return cursor_backing_pages_;
  }

  zx_status_t CreateScanout(uint32_t width, uint32_t height) {
    zx::vmo scanout_vmo;
    size_t scanout_vmo_size = width * height * kPixelSize;
    zx_status_t status = zx::vmo::create(scanout_vmo_size, 0, &scanout_vmo);
    if (status != ZX_OK) {
      return status;
    }
    status = zx::vmar::root_self()->map(
        0, scanout_vmo, 0, scanout_vmo_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
        reinterpret_cast<uintptr_t*>(&scanout_buffer_));
    if (status != ZX_OK) {
      return status;
    }
    return gpu_.scanout()->SetFlushTarget(std::move(scanout_vmo),
                                          scanout_vmo_size, width, height,
                                          width * kPixelSize);
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

  // Attaches randomly-sized backing pages to a resource.
  zx_status_t AttachBacking(
      uint32_t resource_id, uint32_t width, uint32_t height,
      std::vector<std::unique_ptr<BackingPages>>* backing_pages) {
    virtio_gpu_resource_attach_backing_t request = {};
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    request.resource_id = resource_id;

    std::vector<virtio_gpu_mem_entry_t> entries;
    uint32_t size_remaining = width * height * kPixelSize;
    static constexpr uint32_t kPageSize = 4096;
    static constexpr uint32_t kEntryMinPages = 1;
    static constexpr uint32_t kEntryMaxPages = 16;
    while (size_remaining > 0) {
      uint32_t entry_pages =
          kEntryMinPages + (rand() % (kEntryMaxPages - kEntryMinPages + 1));
      uint32_t entry_size = std::min(entry_pages * kPageSize, size_remaining);
      auto backing = std::make_unique<BackingPages>(entry_size);
      virtio_gpu_mem_entry_t entry{};
      entry.addr = reinterpret_cast<uint64_t>(backing->buffer.get());
      entry.length = entry_size;
      backing_pages->push_back(std::move(backing));
      entries.push_back(entry);
      size_remaining -= entry_size;
    }

    request.nr_entries = entries.size();
    virtio_gpu_ctrl_hdr_t response = {};
    zx_status_t status =
        control_queue()
            .BuildDescriptor()
            .AppendReadable(&request, sizeof(request))
            .AppendReadable(entries.data(), entries.size() * sizeof(entries[0]))
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
  VirtioQueueFake control_queue_;
  // Backing pages for resources.
  std::vector<std::unique_ptr<BackingPages>> root_backing_pages_;
  std::vector<std::unique_ptr<BackingPages>> cursor_backing_pages_;

  // A direct pointer into our scanout buffer.
  zx::vmo scanout_vmo_;
  uint8_t* scanout_buffer_ = nullptr;
  size_t scanout_size_ = kDisplayWidth * kDisplayHeight * kPixelSize;
};

TEST_F(VirtioGpuTest, HandleGetDisplayInfo) {
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
  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);
}

TEST_F(VirtioGpuTest, SetScanoutToInvalidResource) {
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
    memset(entry->buffer.get(), 0xff, entry->len);
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
    ASSERT_EQ(
        memcmp(entry->buffer.get(), scanout_buffer() + offset, entry->len), 0);
    offset += entry->len;
  }
}

// Verify a transfer 2d command correctly fills a subregion of the scanout
// buffer.
TEST_F(VirtioGpuTest, HandleTransfer2DSubregion) {
  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);

  static constexpr virtio_gpu_rect_t kTransferRect{37, 41, 43, 47};
  virtio_gpu_transfer_to_host_2d_t request = {};
  request.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  request.resource_id = kRootResourceId;
  request.r = kTransferRect;
  request.offset =
      (kTransferRect.y * kDisplayWidth + kTransferRect.x) * kPixelSize;

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
    memset(entry->buffer.get(), 0xff, entry->len);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(control_queue().HasUsed());
  EXPECT_EQ(sizeof(response), control_queue().NextUsed().len);
  ASSERT_EQ(response.type, VIRTIO_GPU_RESP_OK_NODATA);

  // Send a flush command to draw bytes to our scanout buffer.
  ASSERT_EQ(Flush(), ZX_OK);

  // Verify the sub-region is correctly copied, and the remaining regions are
  // untouched.
  const uint8_t pixel_initial[kPixelSize] = {0, 0, 0, 0};
  const uint8_t pixel_transfer[kPixelSize] = {0xff, 0xff, 0xff, 0xff};
  for (uint32_t row = 0; row < kDisplayHeight; ++row) {
    for (uint32_t col = 0; col < kDisplayWidth; ++col) {
      size_t offset = (row * kDisplayWidth + col) * kPixelSize;
      if (row >= kTransferRect.y &&
          row < kTransferRect.y + kTransferRect.height &&
          col >= kTransferRect.x &&
          col < kTransferRect.x + kTransferRect.width) {
        ASSERT_EQ(memcmp(scanout_buffer() + offset, pixel_transfer, kPixelSize),
                  0);
      } else {
        ASSERT_EQ(memcmp(scanout_buffer() + offset, pixel_initial, kPixelSize),
                  0);
      }
    }
  }
}

// Verifies that cursor virtio commands are handled correctly.
// Note that the response action itself is currently no-op.
TEST_F(VirtioGpuTest, UpdateCursor) {
  ASSERT_EQ(CreateRootResource(), ZX_OK);
  ASSERT_EQ(AttachRootBacking(), ZX_OK);
  ASSERT_EQ(SetScanout(), ZX_OK);
  ASSERT_EQ(CreateCursorResource(), ZX_OK);
  ASSERT_EQ(AttachCursorBacking(), ZX_OK);

  // Initialize the cursor resource to 0xff.
  memset(scanout_buffer(), 0xab, scanout_size());
  for (const auto& entry : cursor_backing_pages()) {
    memset(entry->buffer.get(), 0xff, entry->len);
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
