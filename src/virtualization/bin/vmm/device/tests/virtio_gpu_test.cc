// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/zx/status.h>

#include <virtio/gpu.h>

#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

constexpr uint16_t kNumQueues = 2;
constexpr uint16_t kQueueSize = 16;

constexpr uint32_t kPixelFormat = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
constexpr size_t kPixelSizeInBytes = 4;

// Resource IDs are client allocated, so any value here is fine except for 0. Some GPU commands (ex
// SET_SCANOUT) use resource_id == 0 to mean no resource so some implementations may fail to create
// a resource with resource_id == 0.
//
// Section 5.7.6.8: controlq: ...The driver can use resource_id = 0 to disable a scanout.
constexpr uint32_t kResourceId = 1;
constexpr uint32_t kScanoutId = 0;

static constexpr uint32_t kGpuStartupWidth = 1280;
static constexpr uint32_t kGpuStartupHeight = 720;

constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_gpu#meta/virtio_gpu.cm";
constexpr auto kGraphicalPresenterUrl = "#meta/test_graphical_presenter.cm";

struct VirtioGpuTestParam {
  std::string test_name;
  bool configure_cursor_queue;
};

class VirtioGpuTest : public TestWithDevice,
                      public ::testing::WithParamInterface<VirtioGpuTestParam> {
 protected:
  VirtioGpuTest()
      : control_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        cursor_queue_(phys_mem_, control_queue_.end(), kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    ui_testing::UITestRealm::Config ui_config;
    ui_config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    ui_config.use_flatland = true;
    ui_config.ui_to_client_services = {fuchsia::ui::composition::Flatland::Name_,
                                       fuchsia::ui::composition::Allocator::Name_};
    ui_config.exposed_client_services = {fuchsia::virtualization::hardware::VirtioGpu::Name_};

    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(ui_config));

    constexpr auto kComponentName = "virtio_gpu";
    constexpr auto kGraphicalPresenterComponentName = "graphical_presenter";

    auto realm_builder = ui_test_manager_->AddSubrealm();
    realm_builder.AddChild(kComponentName, kComponentUrl);
    realm_builder.AddChild(kGraphicalPresenterComponentName, kGraphicalPresenterUrl);

    realm_builder
        .AddRoute(Route{
            .capabilities =
                {
                    Protocol{fuchsia::logger::LogSink::Name_},
                    Protocol{fuchsia::ui::composition::Flatland::Name_},
                },
            .source = ParentRef(),
            .targets = {ChildRef{kComponentName}, ChildRef{kGraphicalPresenterComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::sysmem::Allocator::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                                Protocol{fuchsia::ui::composition::Allocator::Name_},
                                Protocol{fuchsia::ui::scenic::Scenic::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioGpu::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::ui::app::ViewProvider::Name_},
                            },
                        .source = ChildRef{kGraphicalPresenterComponentName},
                        .targets = {ParentRef()}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::element::GraphicalPresenter::Name_},
                            },
                        .source = ChildRef{kGraphicalPresenterComponentName},
                        .targets = {ChildRef{kComponentName}}});

    ui_test_manager_->BuildRealm();
    exposed_client_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    ui_test_manager_->InitializeScene();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(cursor_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = exposed_client_services_->Connect(gpu_.NewRequest());
    ASSERT_EQ(ZX_OK, status);

    status = gpu_->Start(std::move(start_info), nullptr, nullptr);
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&control_queue_, &cursor_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = gpu_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
      if (!GetParam().configure_cursor_queue) {
        break;
      }
    }

    // Finish negotiating features.
    status = gpu_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  std::optional<fuchsia::ui::observation::geometry::ViewDescriptor> FindGpuView() {
    auto presenter_koid = ui_test_manager_->ClientViewRefKoid();
    if (!presenter_koid) {
      return {};
    }
    auto presenter = ui_test_manager_->FindViewFromSnapshotByKoid(*presenter_koid);
    if (!presenter || !presenter->has_children() || presenter->children().empty()) {
      return {};
    }
    return ui_test_manager_->FindViewFromSnapshotByKoid(presenter->children()[0]);
  }

  zx::status<std::pair<uint32_t, uint32_t>> WaitForScanout() {
    bool view_created =
        RunLoopWithTimeoutOrUntil([this] { return FindGpuView().has_value(); }, zx::sec(20));
    if (!view_created) {
      return zx::error(ZX_ERR_TIMED_OUT);
    }
    auto gpu_view = *FindGpuView();
    const auto& extent = gpu_view.layout().extent;
    return zx::ok(std::make_pair(std::round(extent.max.x - extent.min.x),
                                 std::round(extent.max.y - extent.min.y)));
  }

  template <typename T>
  zx_status_t SendRequest(const T& request, virtio_gpu_ctrl_hdr_t** response) {
    zx_status_t status = DescriptorChainBuilder(control_queue_)
                             .AppendReadableDescriptor(&request, sizeof(request))
                             .AppendWritableDescriptor(response, sizeof(virtio_gpu_ctrl_hdr_t))
                             .Build();
    if (status != ZX_OK) {
      return status;
    }

    status = gpu_->NotifyQueue(0);
    if (status != ZX_OK) {
      return status;
    }

    return WaitOnInterrupt();
  }

  void ResourceCreate2d() {
    virtio_gpu_ctrl_hdr_t* response;
    ASSERT_EQ(SendRequest(
                  virtio_gpu_resource_create_2d_t{
                      .hdr = {.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D},
                      .resource_id = kResourceId,
                      .format = kPixelFormat,
                      .width = kGpuStartupWidth,
                      .height = kGpuStartupHeight,
                  },
                  &response),
              ZX_OK);
    ASSERT_EQ(VIRTIO_GPU_RESP_OK_NODATA, response->type);
  }

  void ResourceAttachBacking() {
    virtio_gpu_ctrl_hdr_t* response;
    ASSERT_EQ(SendRequest(
                  virtio_gpu_resource_attach_backing_t{
                      .hdr = {.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING},
                      .resource_id = kResourceId,
                      .nr_entries = 0,
                  },
                  &response),
              ZX_OK);
    ASSERT_EQ(VIRTIO_GPU_RESP_OK_NODATA, response->type);
  }

  void SetScanout(uint32_t resource_id, uint32_t response_type) {
    virtio_gpu_ctrl_hdr_t* response;
    ASSERT_EQ(SendRequest(
                  virtio_gpu_set_scanout_t{
                      .hdr = {.type = VIRTIO_GPU_CMD_SET_SCANOUT},
                      .r = {.x = 0, .y = 0, .width = kGpuStartupWidth, .height = kGpuStartupHeight},
                      .scanout_id = kScanoutId,
                      .resource_id = resource_id,
                  },
                  &response),
              ZX_OK);
    EXPECT_EQ(response_type, response->type);
  }

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioGpuSyncPtr gpu_;
  VirtioQueueFake control_queue_;
  VirtioQueueFake cursor_queue_;
  std::unique_ptr<sys::ServiceDirectory> exposed_client_services_;
};

TEST_P(VirtioGpuTest, GetDisplayInfo) {
  auto geometry = WaitForScanout();
  ASSERT_TRUE(geometry.is_ok());
  auto [gpu_width, gpu_height] = *geometry;

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
  EXPECT_EQ(response->pmodes[0].r.width, gpu_width);
  EXPECT_EQ(response->pmodes[0].r.height, gpu_height);
}

TEST_P(VirtioGpuTest, SetScanout) {
  ASSERT_TRUE(WaitForScanout().is_ok());
  ResourceCreate2d();
  ResourceAttachBacking();
  SetScanout(kResourceId, VIRTIO_GPU_RESP_OK_NODATA);
}

TEST_P(VirtioGpuTest, SetScanoutWithInvalidResourceId) {
  ASSERT_TRUE(WaitForScanout().is_ok());
  ResourceCreate2d();
  ResourceAttachBacking();
  SetScanout(UINT32_MAX, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
}

TEST_P(VirtioGpuTest, CreateLargeResource) {
  virtio_gpu_ctrl_hdr_t* response;
  ASSERT_EQ(SendRequest(
                virtio_gpu_resource_create_2d_t{
                    .hdr = {.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D},
                    .resource_id = kResourceId,
                    .width = UINT32_MAX,
                    .height = UINT32_MAX,
                },
                &response),
            ZX_OK);
  EXPECT_EQ(response->type, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
}

TEST_P(VirtioGpuTest, InvalidTransferToHostParams) {
  ResourceCreate2d();
  ResourceAttachBacking();

  // Select a x/y/width/height values that overflow in a way that (x+width)
  // and (y+height) are within the buffer, but other calculations will not be.
  constexpr virtio_gpu_rect_t kBadRectangle = {
      .x = 0x0004'c000,
      .y = 0x0000'0008,
      .width = 0xfffb'4500,
      .height = 0x0000'02c8,
  };
  static_assert(kBadRectangle.width + kBadRectangle.x <= kGpuStartupWidth);
  static_assert(kBadRectangle.height + kBadRectangle.y <= kGpuStartupHeight);

  virtio_gpu_ctrl_hdr_t* response;
  ASSERT_EQ(
      SendRequest(
          virtio_gpu_transfer_to_host_2d_t{
              .hdr = {.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D},
              .r = kBadRectangle,
              .offset = (kBadRectangle.y * kGpuStartupWidth + kBadRectangle.x) * kPixelSizeInBytes,
              .resource_id = kResourceId,
          },
          &response),
      ZX_OK);
  EXPECT_EQ(response->type, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
}

INSTANTIATE_TEST_SUITE_P(VirtioGpuComponentsTest, VirtioGpuTest,
                         testing::Values(VirtioGpuTestParam{"cursorq", true},
                                         VirtioGpuTestParam{"nocursorq", false}),
                         [](const testing::TestParamInfo<VirtioGpuTestParam>& info) {
                           return info.param.test_name;
                         });
}  // namespace
