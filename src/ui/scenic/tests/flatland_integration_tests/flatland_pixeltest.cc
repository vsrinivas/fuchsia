// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"
#include "src/ui/testing/util/screenshot_helper.h"

namespace integration_tests {

namespace fuc = fuchsia::ui::composition;

using component_testing::RealmRoot;

constexpr fuc::TransformId kRootTransform{.value = 1};

// Test fixture that sets up an environment with a Scenic we can connect to.
class FlatlandPixelTestBase : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder()
            .AddRealmProtocol(fuc::Flatland::Name_)
            .AddRealmProtocol(fuc::FlatlandDisplay::Name_)
            .AddRealmProtocol(fuc::Screenshot::Name_)
            .AddRealmProtocol(fuc::Allocator::Name_)
            .AddRealmProtocol(fuchsia::ui::display::singleton::Info::Name_)
            .Build());

    // Connect to sysmem service.
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(sysmem_allocator_.NewRequest());

    flatland_display_ = realm_->Connect<fuc::FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    flatland_allocator_ = realm_->ConnectSync<fuc::Allocator>();

    // Create a root view.
    root_flatland_ = realm_->Connect<fuc::Flatland>();
    root_flatland_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    // Attach |root_flatland_| as the only Flatland under |flatland_display_|.
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());
    fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
    root_flatland_->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), {},
                                parent_viewport_watcher.NewRequest());
    BlockingPresent(root_flatland_);

    // Get the display's width and height.
    auto singleton_display = realm_->Connect<fuchsia::ui::display::singleton::Info>();
    std::optional<fuchsia::ui::display::singleton::Metrics> info;
    singleton_display->GetMetrics([&info](auto result) { info = std::move(result); });
    RunLoopUntil([&info] { return info.has_value(); });

    display_width_ = info->extent_in_px().width;
    display_height_ = info->extent_in_px().height;

    screenshotter_ = realm_->ConnectSync<fuc::Screenshot>();
  }

 protected:
  // Invokes Flatland.Present() and waits for a response from Scenic that the frame has been
  // presented.
  void BlockingPresent(fuc::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 SetConstraintsAndAllocateBuffer(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
      fuchsia::sysmem::BufferCollectionConstraints constraints) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    auto status =
        sysmem_allocator_->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
    FX_CHECK(status == ZX_OK);

    status = buffer_collection->SetConstraints(true, constraints);
    FX_CHECK(status == ZX_OK);
    zx_status_t allocation_status = ZX_OK;

    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};

    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    FX_CHECK(status == ZX_OK);
    FX_CHECK(allocation_status == ZX_OK);
    EXPECT_EQ(constraints.min_buffer_count, buffer_collection_info.buffer_count);
    FX_CHECK(buffer_collection->Close() == ZX_OK);
    return buffer_collection_info;
  }

  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  fuc::AllocatorSyncPtr flatland_allocator_;
  fuc::FlatlandPtr root_flatland_;
  fuc::ScreenshotSyncPtr screenshotter_;

 private:
  std::unique_ptr<RealmRoot> realm_;
  fuc::FlatlandDisplayPtr flatland_display_;
};

class YUVParameterizedPixelTest
    : public FlatlandPixelTestBase,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {
 public:
  fuchsia::sysmem::BufferCollectionConstraints GetBufferConstraints(
      fuchsia::sysmem::PixelFormatType pixel_format) {
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = {.ram_domain_supported = true,
                                             .cpu_domain_supported = true};

    constraints.usage = fuchsia::sysmem::BufferUsage{.cpu = fuchsia::sysmem::cpuUsageWriteOften};

    constraints.min_buffer_count = 1;

    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints.pixel_format.type = pixel_format;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
    image_constraints.required_min_coded_width = display_width_;
    image_constraints.required_min_coded_height = display_height_;
    image_constraints.required_max_coded_width = display_width_;
    image_constraints.required_max_coded_height = display_height_;

    return constraints;
  }
};

INSTANTIATE_TEST_SUITE_P(YuvPixelFormats, YUVParameterizedPixelTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::NV12,
                                           fuchsia::sysmem::PixelFormatType::I420));

TEST_P(YUVParameterizedPixelTest, YUVTest) {
  // TODO(fxb/59804): Skip this test for AEMU as YUV sysmem images are not supported yet.
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

  auto [local_token, scenic_token] = utils::CreateSysmemTokens(sysmem_allocator_.get());

  // Send one token to Flatland Allocator.
  allocation::BufferCollectionImportExportTokens bc_tokens =
      allocation::BufferCollectionImportExportTokens::New();
  fuc::RegisterBufferCollectionArgs rbc_args = {};
  rbc_args.set_export_token(std::move(bc_tokens.export_token));
  rbc_args.set_buffer_collection_token(std::move(scenic_token));
  fuc::Allocator_RegisterBufferCollection_Result result;
  flatland_allocator_->RegisterBufferCollection(std::move(rbc_args), &result);
  ASSERT_FALSE(result.is_err());

  // Use the local token to allocate a protected buffer.
  auto info =
      SetConstraintsAndAllocateBuffer(std::move(local_token), GetBufferConstraints(GetParam()));

  // Write the pixel values to the VMO.
  const uint32_t num_pixels = display_width_ * display_height_;
  const uint64_t image_vmo_bytes = (3 * num_pixels) / 2;
  zx::vmo& image_vmo = info.buffers[0].vmo;
  zx_status_t status = zx::vmo::create(image_vmo_bytes, 0, &image_vmo);
  EXPECT_EQ(ZX_OK, status);
  uint8_t* vmo_base;
  status = zx::vmar::root_self()->map(ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, 0, image_vmo, 0,
                                      image_vmo_bytes, reinterpret_cast<uintptr_t*>(&vmo_base));
  EXPECT_EQ(ZX_OK, status);

  static const uint8_t kYValue = 110;
  static const uint8_t kUValue = 192;
  static const uint8_t kVValue = 192;

  // Set all the Y pixels at full res.
  for (uint32_t i = 0; i < num_pixels; ++i) {
    vmo_base[i] = kYValue;
  }

  if (GetParam() == fuchsia::sysmem::PixelFormatType::NV12) {
    // Set all the UV pixels pairwise at half res.
    for (uint32_t i = num_pixels; i < image_vmo_bytes; i += 2) {
      vmo_base[i] = kUValue;
      vmo_base[i + 1] = kVValue;
    }
  } else if (GetParam() == fuchsia::sysmem::PixelFormatType::I420) {
    for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 4; ++i) {
      vmo_base[i] = kUValue;
    }
    for (uint32_t i = num_pixels + num_pixels / 4; i < image_vmo_bytes; ++i) {
      vmo_base[i] = kVValue;
    }
  } else {
    FX_NOTREACHED();
  }

  // Flush the cache after writing to host VMO.
  EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_base, image_vmo_bytes,
                                  ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));

  // Create the image in the Flatland instance.
  fuc::ImageProperties image_properties = {};
  image_properties.set_size({display_width_, display_height_});
  const fuc::ContentId kImageContentId{.value = 1};

  root_flatland_->CreateImage(kImageContentId, std::move(bc_tokens.import_token), 0,
                              std::move(image_properties));

  // Present the created Image.
  root_flatland_->CreateTransform(kRootTransform);
  root_flatland_->SetRootTransform(kRootTransform);
  root_flatland_->SetContent(kRootTransform, kImageContentId);
  BlockingPresent(root_flatland_);

  // TODO(fxbug.dev/65765): provide reasoning for why this is the correct expected color.
  const ui_testing::Pixel expected_pixel(255, 85, 249, 255);

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);
  auto histogram = screenshot.Histogram();
  EXPECT_EQ(histogram[expected_pixel], num_pixels);
}

}  // namespace integration_tests
