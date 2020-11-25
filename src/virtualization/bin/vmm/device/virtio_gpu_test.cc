// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>

#include <virtio/gpu.h>

#include "src/virtualization/bin/vmm/device/gpu.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioGpuUrl[] = "fuchsia-pkg://fuchsia.com/virtio_gpu#meta/virtio_gpu.cmx";
static constexpr uint16_t kNumQueues = 2;
static constexpr uint16_t kQueueSize = 16;

static constexpr uint32_t kPixelFormat = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
static constexpr uint32_t kResourceId = 0;
static constexpr uint32_t kScanoutId = 0;

class VirtioGpuTest : public TestWithDevice, public fuchsia::ui::scenic::testing::Scenic_TestBase {
 protected:
  VirtioGpuTest()
      : control_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        cursor_queue_(phys_mem_, control_queue_.end(), kQueueSize) {}

  void SetUp() override {
    auto env_services = CreateServices();
    zx_status_t status = env_services->AddService(bindings_.GetHandler(this));
    ASSERT_EQ(ZX_OK, status);

    // Launch device process.
    fuchsia::virtualization::hardware::StartInfo start_info;
    status = LaunchDevice(kVirtioGpuUrl, cursor_queue_.end(), &start_info, std::move(env_services));
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(gpu_.NewRequest());
    RunLoopUntilIdle();

    status = gpu_->Start(std::move(start_info), nullptr);
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&control_queue_, &cursor_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = gpu_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }
  }

  void NotImplemented_(const std::string& name) override {
    printf("Not implemented: Scenic::%s\n", name.data());
  }

  void ResourceCreate2d() {
    virtio_gpu_resource_create_2d_t request = {
        .hdr = {.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D},
        .resource_id = kResourceId,
        .format = kPixelFormat,
        .width = kGpuStartupWidth,
        .height = kGpuStartupHeight,
    };
    virtio_gpu_ctrl_hdr_t* response;
    zx_status_t status = DescriptorChainBuilder(control_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build();
    ASSERT_EQ(ZX_OK, status);

    status = gpu_->NotifyQueue(0);
    ASSERT_EQ(ZX_OK, status);
    status = WaitOnInterrupt();
    ASSERT_EQ(ZX_OK, status);

    ASSERT_EQ(VIRTIO_GPU_RESP_OK_NODATA, response->type);
  }

  void ResourceAttachBacking() {
    virtio_gpu_resource_attach_backing_t request = {
        .hdr = {.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING},
        .resource_id = kResourceId,
        .nr_entries = 0,
    };
    virtio_gpu_ctrl_hdr_t* response;
    zx_status_t status = DescriptorChainBuilder(control_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build();
    ASSERT_EQ(ZX_OK, status);

    status = gpu_->NotifyQueue(0);
    ASSERT_EQ(ZX_OK, status);
    status = WaitOnInterrupt();
    ASSERT_EQ(ZX_OK, status);

    ASSERT_EQ(VIRTIO_GPU_RESP_OK_NODATA, response->type);
  }

  void SetScanout(uint32_t resource_id, uint32_t response_type) {
    virtio_gpu_set_scanout_t request = {
        .hdr = {.type = VIRTIO_GPU_CMD_SET_SCANOUT},
        .r = {.x = 0, .y = 0, .width = kGpuStartupWidth, .height = kGpuStartupHeight},
        .scanout_id = kScanoutId,
        .resource_id = resource_id,
    };
    virtio_gpu_ctrl_hdr_t* response;
    zx_status_t status = DescriptorChainBuilder(control_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(&response, sizeof(*response))
                             .Build();
    ASSERT_EQ(ZX_OK, status);

    status = gpu_->NotifyQueue(0);
    ASSERT_EQ(ZX_OK, status);
    status = WaitOnInterrupt();
    ASSERT_EQ(ZX_OK, status);

    EXPECT_EQ(response_type, response->type);
  }

  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioGpuSyncPtr gpu_;
  VirtioQueueFake control_queue_;
  VirtioQueueFake cursor_queue_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
};

TEST_F(VirtioGpuTest, GetDisplayInfo) {
  virtio_gpu_ctrl_hdr_t request = {
      .type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
  };
  virtio_gpu_resp_display_info_t* response;
  zx_status_t status = DescriptorChainBuilder(control_queue_)
                           .AppendReadableDescriptor(&request, sizeof(request))
                           .AppendWritableDescriptor(&response, sizeof(*response))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = gpu_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(response->hdr.type, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
  EXPECT_EQ(response->pmodes[0].r.x, 0u);
  EXPECT_EQ(response->pmodes[0].r.y, 0u);
  EXPECT_EQ(response->pmodes[0].r.width, kGpuStartupWidth);
  EXPECT_EQ(response->pmodes[0].r.height, kGpuStartupHeight);
}

TEST_F(VirtioGpuTest, SetScanout) {
  ResourceCreate2d();
  ResourceAttachBacking();
  SetScanout(kResourceId, VIRTIO_GPU_RESP_OK_NODATA);
}

TEST_F(VirtioGpuTest, SetScanoutWithInvalidResourceId) {
  ResourceCreate2d();
  ResourceAttachBacking();
  SetScanout(UINT32_MAX, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
}
