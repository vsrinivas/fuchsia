// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screen_capture2/screen_capture2_manager.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/ui/composition/internal/cpp/fidl.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"
#include "src/ui/scenic/lib/screen_capture2/tests/common.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using testing::_;

using allocation::Allocator;
using allocation::BufferCollectionImporter;
using flatland::FlatlandManager;
using flatland::ImageRect;
using flatland::MockFlatlandPresenter;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::internal::FrameInfo;
using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;
using screen_capture::ScreenCaptureBufferCollectionImporter;

namespace screen_capture2 {
namespace test {

class ScreenCapture2ManagerTest : public gtest::TestLoopFixture {
 public:
  ScreenCapture2ManagerTest() = default;

  void SetUp() override {
    // Create the SysmemAllocator.
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr();

    renderer_ = std::make_shared<flatland::NullRenderer>();
    importer_ = std::make_unique<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("ScreenCapture2ManagerTest"), renderer_,
        /*enable_copy_fallback=*/false);

    manager_ = std::make_unique<ScreenCapture2Manager>(
        renderer_, importer_, std::bind(&ScreenCapture2ManagerTest::GetRenderables, this));
  }

  void TearDown() override {
    manager_.reset();
    RunLoopUntilIdle();
  }

  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> CreateScreenCapture() {
    fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc;
    manager_->CreateClient(sc.NewRequest());
    return sc;
  }

  flatland::Renderables GetRenderables() {
    return std::make_pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>>({}, {});
  }

  void ConfigureScreenCapture(
      fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture>& sc,
      BufferCount buffer_count, uint32_t image_width, uint32_t image_height) {
    allocation::BufferCollectionImportExportTokens ref_pair =
        allocation::BufferCollectionImportExportTokens::New();

    std::shared_ptr<Allocator> flatland_allocator =
        CreateAllocator(importer_, context_provider_.context());
    CreateBufferCollectionInfo2WithConstraints(
        utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
        std::move(ref_pair.export_token), flatland_allocator, sysmem_allocator_.get());

    ScreenCaptureConfig args;
    args.set_import_token(std::move(ref_pair.import_token));
    args.set_image_size({image_width, image_height});

    fpromise::result<void, ScreenCaptureError> configure_result;
    sc->Configure(std::move(args),
                  [&configure_result](fpromise::result<void, ScreenCaptureError> result) {
                    EXPECT_FALSE(result.is_error());
                    configure_result = result;
                  });
    RunLoopUntilIdle();
    EXPECT_TRUE(configure_result.is_ok());
  }

 protected:
  std::shared_ptr<flatland::Renderer> renderer_;
  std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter> importer_;
  std::unique_ptr<ScreenCapture2Manager> manager_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(ScreenCapture2ManagerTest, CreateClients) {
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc1 = CreateScreenCapture();
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc2 = CreateScreenCapture();

  RunLoopUntilIdle();
  EXPECT_TRUE(sc1.is_bound());
  EXPECT_TRUE(sc2.is_bound());

  EXPECT_EQ(manager_->client_count(), 2ul);
}

TEST_F(ScreenCapture2ManagerTest, ClientDiesBeforeManager) {
  {
    fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc =
        CreateScreenCapture();
    RunLoopUntilIdle();
    EXPECT_TRUE(sc.is_bound());
    EXPECT_EQ(manager_->client_count(), 1ul);
    // |sc| falls out of scope.
  }
  RunLoopUntilIdle();

  EXPECT_EQ(manager_->client_count(), 0ul);
}

TEST_F(ScreenCapture2ManagerTest, ManagerDiesBeforeClients) {
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc1 = CreateScreenCapture();
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc2 = CreateScreenCapture();

  RunLoopUntilIdle();
  EXPECT_TRUE(sc1.is_bound());
  EXPECT_TRUE(sc2.is_bound());

  EXPECT_EQ(manager_->client_count(), 2ul);

  manager_.reset();
  RunLoopUntilIdle();
  EXPECT_FALSE(sc1.is_bound());
  EXPECT_FALSE(sc2.is_bound());
}

TEST_F(ScreenCapture2ManagerTest, Client_Configure) {
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc = CreateScreenCapture();
  RunLoopUntilIdle();
  EXPECT_TRUE(sc.is_bound());
  EXPECT_EQ(manager_->client_count(), 1ul);

  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  ConfigureScreenCapture(sc, buffer_count, image_width, image_height);
}

TEST_F(ScreenCapture2ManagerTest, Manager_OnCpuWorkDone) {
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc = CreateScreenCapture();
  RunLoopUntilIdle();
  EXPECT_TRUE(sc.is_bound());
  EXPECT_EQ(manager_->client_count(), 1ul);

  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  ConfigureScreenCapture(sc, buffer_count, image_width, image_height);

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
  sc->GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result = std::move(result);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(gnf_result.is_ok());
  FrameInfo info = std::move(gnf_result.value());

  zx::eventpair token = std::move(*info.mutable_buffer_release_token());
  EXPECT_EQ(token.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
  RunLoopUntilIdle();

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result2;
  bool callback_called = false;
  sc->GetNextFrame(
      [&gnf_result2, &callback_called](fpromise::result<FrameInfo, ScreenCaptureError> result) {
        EXPECT_FALSE(result.is_error());
        gnf_result2 = std::move(result);
        callback_called = true;
      });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);

  // Since |recieved_last_frame_| is true, GetNextFrame() will be hanging for new frame.
  manager_->OnCpuWorkDone();
  RunLoopUntilIdle();

  EXPECT_TRUE(gnf_result2.is_ok());
  FrameInfo info2 = std::move(gnf_result2.value());
  EXPECT_EQ(info2.buffer_index(), info.buffer_index());
  EXPECT_TRUE(callback_called);
}

// Expects |render_frame_in_use_| to lock MaybeRenderFrame() and Client to recieve expected frame.
TEST_F(ScreenCapture2ManagerTest, ManagerClient_BothWantNewFrame) {
  fidl::InterfacePtr<fuchsia::ui::composition::internal::ScreenCapture> sc = CreateScreenCapture();
  RunLoopUntilIdle();
  EXPECT_TRUE(sc.is_bound());
  EXPECT_EQ(manager_->client_count(), 1ul);

  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  ConfigureScreenCapture(sc, buffer_count, image_width, image_height);

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
  sc->GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result = std::move(result);
  });
  manager_->OnCpuWorkDone();
  RunLoopUntilIdle();
  EXPECT_TRUE(gnf_result.is_ok());
}

}  // namespace test
}  // namespace screen_capture2
