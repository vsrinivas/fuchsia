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
constexpr auto kEpsilon = 1;

fuc::ColorRgba GetColorInFloat(ui_testing::Pixel color) {
  return {static_cast<float>(color.red) / 255.f, static_cast<float>(color.green) / 255.f,
          static_cast<float>(color.blue) / 255.f, static_cast<float>(color.alpha) / 255.f};
}

// Asserts whether the BGRA channel value difference between |actual| and |expected| is at most
// |kEpsilon|.
void CompareColor(ui_testing::Pixel actual, ui_testing::Pixel expected) {
  ASSERT_NEAR(actual.blue, expected.blue, kEpsilon);
  ASSERT_NEAR(actual.green, expected.green, kEpsilon);
  ASSERT_NEAR(actual.red, expected.red, kEpsilon);
  ASSERT_NEAR(actual.alpha, expected.alpha, kEpsilon);
}

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

    // Create the root transform.
    root_flatland_->CreateTransform(kRootTransform);
    root_flatland_->SetRootTransform(kRootTransform);

    // Get the display's width and height.
    auto singleton_display = realm_->Connect<fuchsia::ui::display::singleton::Info>();
    std::optional<fuchsia::ui::display::singleton::Metrics> info;
    singleton_display->GetMetrics([&info](auto result) { info = std::move(result); });
    RunLoopUntil([&info] { return info.has_value(); });

    display_width_ = info->extent_in_px().width;
    display_height_ = info->extent_in_px().height;

    screenshotter_ = realm_->ConnectSync<fuc::Screenshot>();
  }

  // Draws a rectangle of size |width|*|height|, color |color|, opacity |opacity| and origin
  // (|x|,|y|) in |flatland|'s view.
  // Note: |BlockingPresent| must be called after this function to present the rectangle on the
  // display.
  void DrawRectangle(fuc::FlatlandPtr& flatland, uint32_t width, uint32_t height, int32_t x,
                     int32_t y, ui_testing::Pixel color,
                     fuc::BlendMode blend_mode = fuc::BlendMode::SRC, float opacity = 1.f) {
    const fuc::ContentId kFilledRectId = {get_next_resource_id()};
    const fuc::TransformId kTransformId = {get_next_resource_id()};

    flatland->CreateFilledRect(kFilledRectId);
    flatland->SetSolidFill(kFilledRectId, GetColorInFloat(color), {width, height});

    // Associate the rect with a transform.
    flatland->CreateTransform(kTransformId);
    flatland->SetContent(kTransformId, kFilledRectId);
    flatland->SetTranslation(kTransformId, {x, y});

    // Set the opacity and the BlendMode for the rectangle.
    flatland->SetImageBlendingFunction(kFilledRectId, blend_mode);
    flatland->SetOpacity(kTransformId, opacity);

    // Attach the transform to the view.
    flatland->AddChild(fuchsia::ui::composition::TransformId{kRootTransform}, kTransformId);
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
  std::unique_ptr<RealmRoot> realm_;
  uint64_t get_next_resource_id() { return resource_id_++; }

 private:
  uint64_t resource_id_ = kRootTransform.value + 1;
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
  root_flatland_->SetContent(kRootTransform, kImageContentId);
  BlockingPresent(root_flatland_);

  // TODO(fxbug.dev/65765): provide reasoning for why this is the correct expected color.
  const ui_testing::Pixel expected_pixel(255, 85, 249, 255);

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);
  auto histogram = screenshot.Histogram();
  EXPECT_EQ(histogram[expected_pixel], num_pixels);
}

// Draws and tests the following coordinate test pattern without views:
// ___________________________________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
TEST_F(FlatlandPixelTestBase, CoordinateViewTest) {
  const uint32_t view_width = display_width_;
  const uint32_t view_height = display_height_;

  const uint32_t pane_width =
      static_cast<uint32_t>(std::ceil(static_cast<float>(view_width) / 2.f));

  const uint32_t pane_height =
      static_cast<uint32_t>(std::ceil(static_cast<float>(view_height) / 2.f));

  // Draw the rectangles in the quadrants.
  for (uint32_t i = 0; i < 2; i++) {
    for (uint32_t j = 0; j < 2; j++) {
      ui_testing::Pixel color(static_cast<uint8_t>(j * 255), 0, static_cast<uint8_t>(i * 255), 255);
      DrawRectangle(root_flatland_, pane_width, pane_height, i * pane_width, j * pane_height,
                    color);
    }
  }

  // Draw the rectangle in the center.
  DrawRectangle(root_flatland_, view_width / 4, view_height / 4, 3 * view_width / 8,
                3 * view_height / 8, ui_testing::Screenshot::kGreen);

  BlockingPresent(root_flatland_);

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);

  // Check pixel content at all four corners.
  EXPECT_EQ(screenshot.GetPixelAt(0, 0), ui_testing::Screenshot::kBlack);  // Top left
  EXPECT_EQ(screenshot.GetPixelAt(0, screenshot.height() - 1),
            ui_testing::Screenshot::kBlue);  // Bottom left
  EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() - 1, 0),
            ui_testing::Screenshot::kRed);  // Top right
  EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() - 1, screenshot.height() - 1),
            ui_testing::Screenshot::kMagenta);  // Bottom right

  // Check pixel content at center of each rectangle.
  EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 4, screenshot.height() / 4),
            ui_testing::Screenshot::kBlack);  // Top left
  EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 4, (3 * screenshot.height()) / 4),
            ui_testing::Screenshot::kBlue);  // Bottom left
  EXPECT_EQ(screenshot.GetPixelAt((3 * screenshot.width()) / 4, screenshot.height() / 4),
            ui_testing::Screenshot::kRed);  // Top right
  EXPECT_EQ(screenshot.GetPixelAt((3 * screenshot.width()) / 4, (3 * screenshot.height()) / 4),
            ui_testing::Screenshot::kMagenta);  // Bottom right
  EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 2, screenshot.height() / 2),
            ui_testing::Screenshot::kGreen);  // Center
}

struct OpacityTestParams {
  float opacity;
  ui_testing::Pixel expected_pixel;
};

class ParameterizedOpacityPixelTest : public FlatlandPixelTestBase,
                                      public ::testing::WithParamInterface<OpacityTestParams> {};

// We use the same background/foreground color for each test iteration, but
// vary the opacity.  When the opacity is 0% we expect the pure background
// color, and when it is 100% we expect the pure foreground color.  When
// opacity is 50% we expect a blend of the two when |f.u.c.BlendMode| is |f.u.c.BlendMode.SRC_OVER|.
INSTANTIATE_TEST_SUITE_P(
    Opacity, ParameterizedOpacityPixelTest,
    ::testing::Values(OpacityTestParams{.opacity = 0.0f, .expected_pixel = {0, 0, 255, 255}},
                      OpacityTestParams{.opacity = 0.5f, .expected_pixel = {0, 188, 188, 255}},
                      OpacityTestParams{.opacity = 1.0f, .expected_pixel = {0, 255, 0, 255}}));

// This test first draws a rectangle of size |display_width_* display_height_| and then draws
// another rectangle having same dimensions on the top.
TEST_P(ParameterizedOpacityPixelTest, OpacityTest) {
  ui_testing::Pixel background_color(ui_testing::Screenshot::kRed);
  ui_testing::Pixel foreground_color(ui_testing::Screenshot::kGreen);

  // Draw the background rectangle.
  DrawRectangle(root_flatland_, display_width_, display_height_, 0, 0, background_color);

  // Draw the foreground rectangle.
  DrawRectangle(root_flatland_, display_width_, display_height_, 0, 0, foreground_color,
                fuc::BlendMode::SRC_OVER, GetParam().opacity);

  BlockingPresent(root_flatland_);

  const auto num_pixels = display_width_ * display_height_;

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);
  auto histogram = screenshot.Histogram();

  // There should be only one color here in the histogram.
  ASSERT_EQ(histogram.size(), 1u);
  CompareColor(histogram.begin()->first, GetParam().expected_pixel);

  EXPECT_EQ(histogram.begin()->second, num_pixels);
}

// This test checks whether any content drawn outside the view bounds are correctly clipped.
// The test draws a scene as shown below:-
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
//  bbbbbbbbbbxxxxxxxxxx
// The first rectangle gets clipped outide the left half of the display and the second rectangle
// gets completely clipped because it was drawn outside of the view bounds.
TEST_F(FlatlandPixelTestBase, ViewBoundClipping) {
  // Create a child view.
  fuc::FlatlandPtr child;
  child = realm_->Connect<fuc::Flatland>();
  uint32_t child_width = 0, child_height = 0;

  auto [view_creation_token, viewport_token] = scenic::ViewCreationTokenPair::New();
  fidl::InterfacePtr<fuc::ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(view_creation_token), scenic::NewViewIdentityOnCreation(), {},
                     parent_viewport_watcher.NewRequest());
  BlockingPresent(child);

  // Connect the child view to the root view.
  const fuc::TransformId viewport_transform = {get_next_resource_id()};
  const fuc::ContentId viewport_content = {get_next_resource_id()};

  root_flatland_->CreateTransform(viewport_transform);
  fuc::ViewportProperties properties;

  // Allow the child view to draw content in the left half of the display.
  properties.set_logical_size({display_width_ / 2, display_height_});
  fidl::InterfacePtr<fuc::ChildViewWatcher> child_view_watcher;
  root_flatland_->CreateViewport(viewport_content, std::move(viewport_token), std::move(properties),
                                 child_view_watcher.NewRequest());
  root_flatland_->SetContent(viewport_transform, viewport_content);
  root_flatland_->AddChild(kRootTransform, viewport_transform);
  BlockingPresent(root_flatland_);

  parent_viewport_watcher->GetLayout([&child_width, &child_height](auto layout_info) {
    child_width = layout_info.logical_size().width;
    child_height = layout_info.logical_size().height;
  });
  RunLoopUntil([&child_width, &child_height] { return child_width > 0 && child_height > 0; });

  // Create the root transform for the child view.
  child->CreateTransform(kRootTransform);
  child->SetRootTransform(kRootTransform);

  const ui_testing::Pixel default_color(0, 0, 0, 0);

  // The child view draws a rectangle partially outside of its view bounds.
  DrawRectangle(child, 2 * child_width, child_height, 0, 0, ui_testing::Screenshot::kBlue);

  // The child view draws a rectangle completely outside its view bounds.
  DrawRectangle(child, 2 * child_width, child_height, display_width_ / 2, display_height_ / 2,
                ui_testing::Screenshot::kGreen);
  BlockingPresent(child);

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);
  EXPECT_EQ(screenshot.GetPixelAt(0, 0), ui_testing::Screenshot::kBlue);
  EXPECT_EQ(screenshot.GetPixelAt(0, display_height_ - 1), ui_testing::Screenshot::kBlue);

  // The top left and bottom right corner of the display lies outside the child view's bounds so
  // we do not see any color there.
  EXPECT_EQ(screenshot.GetPixelAt(display_width_ - 1, 0), default_color);
  EXPECT_EQ(screenshot.GetPixelAt(display_width_ - 1, display_height_ - 1), default_color);

  auto histogram = screenshot.Histogram();
  const auto num_pixels = static_cast<uint32_t>(display_width_ * display_height_);

  // The child view can only draw content inside its view bounds, hence we see |num_pixels/2| pixels
  // for the first rectangle.
  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlue], num_pixels / 2);

  // No pixels are seen for the second rectangle as it was drawn completely outside the view bounds.
  EXPECT_EQ(histogram[ui_testing::Screenshot::kGreen], 0u);
  EXPECT_EQ(histogram[default_color], num_pixels / 2);
}

// This unit test verifies the behavior of view bound clipping when the view exists under a node
// that itself has a translation applied to it. There are two views with a rectangle in each. The
// first view is under a node that is translated (display_width/2, 0). The second view is placed
// under the first transform node, and then translated again by (0, display_height/2). This
// means that what you see on the screen should look like the following:
//
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//
// Where x refers to empty display pixels.
//       v refers to pixels covered by the first view's bounds.
//       r refers to pixels covered by the second view's bounds.
TEST_F(FlatlandPixelTestBase, TranslateInheritsFromParent) {
  // Draw the first rectangle in the top right quadrant.
  const fuc::ContentId kFilledRectId1 = {get_next_resource_id()};
  const fuc::TransformId kTransformId1 = {get_next_resource_id()};

  root_flatland_->CreateFilledRect(kFilledRectId1);
  root_flatland_->SetSolidFill(kFilledRectId1, GetColorInFloat(ui_testing::Screenshot::kBlue),
                               {display_width_ / 2, display_height_ / 2});

  // Associate the rect with a transform.
  root_flatland_->CreateTransform(kTransformId1);
  root_flatland_->SetContent(kTransformId1, kFilledRectId1);
  root_flatland_->SetTranslation(kTransformId1, {static_cast<int32_t>(display_width_ / 2), 0});

  // Attach the transform to the view.
  root_flatland_->AddChild(kRootTransform, kTransformId1);

  // Draw the second rectangle in the bottom right quadrant.
  const fuc::ContentId kFilledRectId2 = {get_next_resource_id()};
  const fuc::TransformId kTransformId2 = {get_next_resource_id()};

  root_flatland_->CreateFilledRect(kFilledRectId2);
  root_flatland_->SetSolidFill(kFilledRectId2, GetColorInFloat(ui_testing::Screenshot::kGreen),
                               {display_width_ / 2, display_height_ / 2});

  // Associate the rect with a transform.
  root_flatland_->CreateTransform(kTransformId2);
  root_flatland_->SetContent(kTransformId2, kFilledRectId2);
  root_flatland_->SetTranslation(kTransformId2, {0, static_cast<int32_t>(display_height_ / 2)});

  // Add the |kTransformId2| as the child of |kTransformId1| so that its origin is translated to the
  // center of the display.
  root_flatland_->AddChild(kTransformId1, kTransformId2);
  BlockingPresent(root_flatland_);

  const ui_testing::Pixel default_color(0, 0, 0, 0);

  auto screenshot = TakeScreenshot(screenshotter_, display_width_, display_height_);

  EXPECT_EQ(screenshot.GetPixelAt(0, 0), default_color);
  EXPECT_EQ(screenshot.GetPixelAt(0, display_height_ - 1), default_color);

  // Top left corner of the first rectangle drawn.
  EXPECT_EQ(screenshot.GetPixelAt(display_width_ / 2, 0), ui_testing::Screenshot::kBlue);

  // TOp left corner of the second rectangle drawn.
  EXPECT_EQ(screenshot.GetPixelAt(display_width_ / 2, display_height_ / 2),
            ui_testing::Screenshot::kGreen);

  const auto num_pixels = display_width_ * display_height_;

  auto histogram = screenshot.Histogram();

  EXPECT_EQ(histogram[default_color], num_pixels / 2);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlue], num_pixels / 4);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kGreen], num_pixels / 4);
}

}  // namespace integration_tests
