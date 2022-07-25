// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <sys/types.h>
#include <zircon/status.h>

#include <cstdint>
#include <iostream>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"
#include "zircon/system/ulib/fbl/include/fbl/algorithm.h"

namespace integration_tests {

using flatland::MapHostPointer;
using fuchsia::math::SizeU;
using fuchsia::math::Vec;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::Flatland;
using fuchsia::ui::composition::FlatlandDisplay;
using fuchsia::ui::composition::FrameInfo;
using fuchsia::ui::composition::GetNextFrameArgs;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsage;
using fuchsia::ui::composition::ScreenCapture;
using fuchsia::ui::composition::ScreenCaptureConfig;
using fuchsia::ui::composition::ScreenCaptureError;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewRef;
using RealmRoot = component_testing::RealmRoot;

static constexpr uint32_t kBytesPerPixel = 4;

// BGRA
static constexpr uint8_t kRed[] = {0, 0, 255, 255};
static constexpr uint8_t kGreen[] = {0, 255, 0, 255};
static constexpr uint8_t kBlue[] = {255, 0, 0, 255};
static constexpr uint8_t kYellow[] = {0, 255, 255, 255};

bool PixelEquals(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, kBytesPerPixel) == 0; }

void AppendPixel(std::vector<uint8_t>* values, const uint8_t* pixel) {
  values->insert(values->end(), pixel, pixel + kBytesPerPixel);
}

class ScreenCaptureIntegrationTest : public gtest::RealLoopFixture {
 public:
  ScreenCaptureIntegrationTest()
      : realm_(ScenicRealmBuilder()
                   .AddRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
                   .AddRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
                   .AddRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
                   .AddRealmProtocol(fuchsia::ui::composition::ScreenCapture::Name_)
                   .Build()) {
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(sysmem_allocator_.NewRequest());

    flatland_display_ = realm_.Connect<fuchsia::ui::composition::FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to FlatlandDisplay: " << zx_status_get_string(status);
    });

    flatland_allocator_ = realm_.ConnectSync<fuchsia::ui::composition::Allocator>();

    // Set up root view.
    root_session_ = realm_.Connect<fuchsia::ui::composition::Flatland>();
    root_session_.events().OnError = [](fuchsia::ui::composition::FlatlandError error) {
      FAIL() << "Received FlatlandError: " << static_cast<int>(error);
    };
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Flatland: " << zx_status_get_string(status);
    });

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    {
      auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
      flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());

      auto identity = scenic::NewViewIdentityOnCreation();
      root_view_ref_ = fidl::Clone(identity.view_ref);
      root_session_->CreateView2(std::move(child_token), std::move(identity), {},
                                 parent_viewport_watcher.NewRequest());
      parent_viewport_watcher->GetLayout([this](auto layout_info) {
        ASSERT_TRUE(layout_info.has_logical_size());
        const auto [width, height] = layout_info.logical_size();
        display_width_ = width;
        display_height_ = height;
        num_pixels_ = display_width_ * display_height_;
      });
    }
    BlockingPresent(root_session_);

    // Wait until we get the display size.
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });

    // Set up the root graph.
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher2;
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    ViewportProperties properties;
    properties.set_logical_size({display_width_, display_height_});
    const TransformId kRootTransform{.value = 1};
    const ContentId kRootContent{.value = 1};
    root_session_->CreateTransform(kRootTransform);
    root_session_->CreateViewport(kRootContent, std::move(parent_token), std::move(properties),
                                  child_view_watcher2.NewRequest());
    root_session_->SetRootTransform(kRootTransform);
    root_session_->SetContent(kRootTransform, kRootContent);
    BlockingPresent(root_session_);

    // Set up the child view.
    child_session_ = realm_.Connect<fuchsia::ui::composition::Flatland>();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher2;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    child_session_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher2.NewRequest());
    child_session_->CreateTransform(kChildRootTransform);
    child_session_->SetRootTransform(kChildRootTransform);
    BlockingPresent(child_session_);

    // Create ScreenCapture client.
    screen_capture_ = realm_.Connect<fuchsia::ui::composition::ScreenCapture>();
    screen_capture_.set_error_handler(
        [](zx_status_t status) { FAIL() << "Lost connection to ScreenCapture"; });
  }

  static flatland::SysmemTokens CreateSysmemTokens(
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
    zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
    status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    status = local_token->Sync();
    EXPECT_EQ(status, ZX_OK);

    return {std::move(local_token), std::move(dup_token)};
  }

  void BlockingPresent(fuchsia::ui::composition::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfoWithConstraints(
      fuchsia::sysmem::BufferCollectionConstraints constraints,
      allocation::BufferCollectionExportToken export_token,
      RegisterBufferCollectionUsage usage) const {
    // Create Buffer Collection for image to add to scene graph.
    RegisterBufferCollectionArgs args = {};

    auto [local_token, dup_token] = CreateSysmemTokens(sysmem_allocator_.get());

    args.set_export_token(std::move(export_token));
    args.set_buffer_collection_token(std::move(dup_token));
    args.set_usage(usage);

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                                 buffer_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);

    buffer_collection->SetConstraints(true, constraints);

    fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result result;

    flatland_allocator_->RegisterBufferCollection(std::move(args), &result);
    EXPECT_FALSE(result.is_err());

    zx_status_t allocation_status;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
    EXPECT_EQ(ZX_OK, buffer_collection->WaitForBuffersAllocated(&allocation_status,
                                                                &buffer_collection_info));
    EXPECT_EQ(ZX_OK, allocation_status);
    EXPECT_EQ(ZX_OK, buffer_collection->Close());

    return buffer_collection_info;
  }

  // This function calls GetNextFrame().
  fpromise::result<FrameInfo, ScreenCaptureError> CaptureScreen(
      fuchsia::ui::composition::ScreenCapturePtr& screencapturer) {
    zx::event event;
    zx::event dup;
    zx_status_t status = zx::event::create(0, &event);
    EXPECT_EQ(status, ZX_OK);
    event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);

    GetNextFrameArgs gnf_args;
    gnf_args.set_event(std::move(dup));

    fpromise::result<FrameInfo, ScreenCaptureError> response;
    bool alloc_result = false;
    screencapturer->GetNextFrame(
        std::move(gnf_args),
        [&response, &alloc_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
          response = std::move(result);
          alloc_result = true;
        });

    RunLoopUntil([&alloc_result] { return alloc_result; });

    if (response.is_ok()) {
      zx::duration kEventDelay = zx::msec(5000);
      status = event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(kEventDelay), nullptr);
      EXPECT_EQ(status, ZX_OK);
    }

    return response;
  }

  const TransformId kChildRootTransform{.value = 1};
  static constexpr zx::duration kEventDelay = zx::msec(5000);

  RealmRoot realm_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  fuchsia::ui::composition::AllocatorSyncPtr flatland_allocator_;
  fuchsia::ui::composition::FlatlandDisplayPtr flatland_display_;
  fuchsia::ui::composition::FlatlandPtr root_session_;
  fuchsia::ui::composition::FlatlandPtr child_session_;
  fuchsia::ui::composition::ScreenCapturePtr screen_capture_;
  fuchsia::ui::views::ViewRef root_view_ref_;

  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  uint32_t num_pixels_ = 0;
};

struct SysmemTokens {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
};

void GenerateImageForFlatlandInstance(uint32_t buffer_collection_index,
                                      fuchsia::ui::composition::FlatlandPtr& flatland,
                                      TransformId parent_transform,
                                      allocation::BufferCollectionImportToken import_token,
                                      SizeU size, Vec translation, uint32_t image_id,
                                      uint32_t transform_id) {
  // Create the image in the Flatland instance.
  fuchsia::ui::composition::ImageProperties image_properties = {};
  image_properties.set_size(size);
  fuchsia::ui::composition::ContentId content_id = {.value = image_id};
  flatland->CreateImage(content_id, std::move(import_token), buffer_collection_index,
                        std::move(image_properties));

  // Add the created image as a child of the parent transform specified. Apply the right size and
  // orientation commands.
  const TransformId kTransform{.value = transform_id};
  flatland->CreateTransform(kTransform);

  flatland->SetContent(kTransform, content_id);
  flatland->SetImageDestinationSize(content_id, {size.width, size.height});
  flatland->SetTranslation(kTransform, translation);

  flatland->AddChild(parent_transform, kTransform);
}

inline uint32_t GetPixelsPerRow(const fuchsia::sysmem::SingleBufferSettings& settings,
                                uint32_t bytes_per_pixel, uint32_t image_width) {
  uint32_t bytes_per_row_divisor = settings.image_format_constraints.bytes_per_row_divisor;
  uint32_t min_bytes_per_row = settings.image_format_constraints.min_bytes_per_row;
  uint32_t bytes_per_row = fbl::round_up(std::max(image_width * bytes_per_pixel, min_bytes_per_row),
                                         bytes_per_row_divisor);
  uint32_t pixels_per_row = bytes_per_row / bytes_per_pixel;

  return pixels_per_row;
}

// This method writes to a sysmem buffer, taking into account any potential stride width
// differences. The method also flushes the cache if the buffer is in RAM domain.
void WriteToSysmemBuffer(const std::vector<uint8_t>& write_values,
                         fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info,
                         uint32_t buffer_collection_idx, uint32_t kBytesPerPixel,
                         uint32_t image_width, uint32_t image_height) {
  uint32_t pixels_per_row =
      GetPixelsPerRow(buffer_collection_info.settings, kBytesPerPixel, image_width);

  MapHostPointer(buffer_collection_info, buffer_collection_idx,
                 [&write_values, pixels_per_row, kBytesPerPixel, image_width, image_height](
                     uint8_t* vmo_host, uint32_t num_bytes) {
                   uint32_t bytes_per_row = pixels_per_row * kBytesPerPixel;
                   uint32_t valid_bytes_per_row = image_width * kBytesPerPixel;

                   EXPECT_GE(bytes_per_row, valid_bytes_per_row);
                   EXPECT_GE(num_bytes, bytes_per_row * image_height);

                   if (bytes_per_row == valid_bytes_per_row) {
                     // Fast path.
                     memcpy(vmo_host, write_values.data(), write_values.size());
                   } else {
                     // Copy over row-by-row.
                     for (size_t i = 0; i < image_height; ++i) {
                       memcpy(&vmo_host[i * bytes_per_row],
                              &write_values[i * image_width * kBytesPerPixel], valid_bytes_per_row);
                     }
                   }
                 });

  // Flush the cache if we are operating in RAM.
  if (buffer_collection_info.settings.buffer_settings.coherency_domain ==
      fuchsia::sysmem::CoherencyDomain::RAM) {
    EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[buffer_collection_idx].vmo.op_range(
                         ZX_VMO_OP_CACHE_CLEAN, 0,
                         buffer_collection_info.settings.buffer_settings.size_bytes, nullptr, 0));
  }
}

// This function returns a linear buffer of pixels of size width * height.
std::vector<uint8_t> ExtractScreenCapture(
    const fpromise::result<FrameInfo, ScreenCaptureError>& frame_result,
    fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info, uint32_t kBytesPerPixel,
    uint32_t render_target_width, uint32_t render_target_height) {
  EXPECT_FALSE(frame_result.is_error());
  uint32_t buffer_id = frame_result.value().buffer_id();
  // Copy ScreenCapture output for inspection. Note that the stride of the buffer may be different
  // than the width of the image, if the width of the image is not a multiple of 64.
  //
  // For instance, is the original image were 1024x600, the new width is 600. 600*4=2400 bytes,
  // which is not a multiple of 64. The next multiple would be 2432, which would mean the buffer
  // is actually a 608x1024 "pixel" buffer, since 2432/4=608. We must account for that 8 byte
  // padding when copying the bytes over to be inspected.
  EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[buffer_id].vmo.op_range(
                       ZX_CACHE_FLUSH_DATA | ZX_VMO_OP_CACHE_INVALIDATE, 0,
                       buffer_collection_info.settings.buffer_settings.size_bytes, nullptr, 0));

  uint32_t pixels_per_row =
      GetPixelsPerRow(buffer_collection_info.settings, kBytesPerPixel, render_target_width);
  std::vector<uint8_t> read_values;
  read_values.resize(static_cast<size_t>(render_target_width) * render_target_height *
                     kBytesPerPixel);

  MapHostPointer(buffer_collection_info, buffer_id,
                 [&read_values, kBytesPerPixel, pixels_per_row, render_target_width,
                  render_target_height](uint8_t* vmo_host, uint32_t num_bytes) {
                   uint32_t bytes_per_row = pixels_per_row * kBytesPerPixel;
                   uint32_t valid_bytes_per_row = render_target_width * kBytesPerPixel;

                   EXPECT_GE(bytes_per_row, valid_bytes_per_row);

                   if (bytes_per_row == valid_bytes_per_row) {
                     // Fast path.
                     memcpy(read_values.data(), vmo_host,
                            static_cast<size_t>(bytes_per_row) * render_target_height);
                   } else {
                     for (size_t i = 0; i < render_target_height; ++i) {
                       memcpy(&read_values[i * render_target_width * kBytesPerPixel],
                              &vmo_host[i * bytes_per_row], valid_bytes_per_row);
                     }
                   }
                 });

  return read_values;
}

TEST_F(ScreenCaptureIntegrationTest, SingleColorUnrotatedScreenshot) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_width_;
  const uint32_t render_target_height = display_height_;

  // Create Buffer Collection for image to add to scene graph.
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, image_width, image_height),
          std::move(ref_pair.export_token), RegisterBufferCollectionUsage::DEFAULT);

  std::vector<uint8_t> write_values;
  for (uint32_t i = 0; i < num_pixels_; ++i) {
    write_values.insert(write_values.end(), kRed, kRed + kBytesPerPixel);
  }

  WriteToSysmemBuffer(write_values, buffer_collection_info, 0, kBytesPerPixel, image_width,
                      image_height);

  GenerateImageForFlatlandInstance(0, child_session_, kChildRootTransform,
                                   std::move(ref_pair.import_token), {image_width, image_height},
                                   {0, 0}, 2, 2);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screencapturing!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);
  sc_args.set_size({render_target_width, render_target_height});

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), write_values.size());

  // Compare read and write values.
  uint32_t num_red = 0;

  for (size_t i = 0; i < read_values.size(); i += kBytesPerPixel) {
    if (PixelEquals(&read_values[i], kRed))
      num_red++;
  }

  EXPECT_EQ(num_red, num_pixels_);
}

// Creates this image:
//          RRRRRRRR
//          RRRRRRRR
//          GGGGGGGG
//          GGGGGGGG
//
// Rotates into this image:
//          GGGGGGGG
//          GGGGGGGG
//          RRRRRRRR
//          RRRRRRRR
TEST_F(ScreenCaptureIntegrationTest, MultiColor180DegreeRotationScreenshot) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_width_;
  const uint32_t render_target_height = display_height_;

  // Create Buffer Collection for image#1 to add to scene graph.
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, display_width_, display_height_),
          std::move(ref_pair.export_token), RegisterBufferCollectionUsage::DEFAULT);

  // Write the image with half green, half red
  std::vector<uint8_t> write_values;
  const uint32_t pixel_color_count = num_pixels_ / 2;

  for (uint32_t i = 0; i < pixel_color_count; ++i) {
    AppendPixel(&write_values, kRed);
  }
  for (uint32_t i = 0; i < pixel_color_count; ++i) {
    AppendPixel(&write_values, kGreen);
  }
  WriteToSysmemBuffer(write_values, buffer_collection_info, 0, kBytesPerPixel, image_width,
                      image_height);

  GenerateImageForFlatlandInstance(0, child_session_, kChildRootTransform,
                                   std::move(ref_pair.import_token), {image_width, image_height},
                                   {0, 0}, 2, 2);

  BlockingPresent(child_session_);

  // The scene graph is now ready for screenshotting!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);
  sc_args.set_size({render_target_width, render_target_height});
  sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_180_DEGREES);

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), write_values.size());

  // Compare read and write values.
  uint32_t num_green = 0;
  uint32_t num_red = 0;

  for (size_t i = 0; i < read_values.size(); i += kBytesPerPixel) {
    if (PixelEquals(&read_values[i], kGreen)) {
      num_green++;
      EXPECT_TRUE(PixelEquals(&write_values[i], kRed));
    } else if (PixelEquals(&read_values[i], kRed)) {
      num_red++;
      EXPECT_TRUE(PixelEquals(&write_values[i], kGreen));
    }
  }

  EXPECT_EQ(num_green, pixel_color_count);
  EXPECT_EQ(num_red, pixel_color_count);
}

// Creates this image:
//          RRRRRGGGGG
//          RRRRRGGGGG
//          YYYYYBBBBB
//          YYYYYBBBBB
//
// Rotates into this image:
//          YYRR
//          YYRR
//          YYRR
//          YYRR
//          YYRR
//          BBGG
//          BBGG
//          BBGG
//          BBGG
//          BBGG
TEST_F(ScreenCaptureIntegrationTest, MultiColor90DegreeRotationScreenshot) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_height_;
  const uint32_t render_target_height = display_width_;

  // Create Buffer Collection for image#1 to add to scene graph.
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, image_width, image_height),
          std::move(ref_pair.export_token), RegisterBufferCollectionUsage::DEFAULT);

  // Write the image with the color scheme displayed in ASCII above.
  std::vector<uint8_t> write_values;

  uint32_t red_pixel_count = 0;
  uint32_t green_pixel_count = 0;
  uint32_t blue_pixel_count = 0;
  uint32_t yellow_pixel_count = 0;
  const uint32_t pixel_color_count = num_pixels_ / 4;

  for (uint32_t i = 0; i < num_pixels_; ++i) {
    uint32_t row = i / image_width;
    uint32_t col = i % image_width;

    // Top-left quadrant
    if (row < image_height / 2 && col < image_width / 2) {
      AppendPixel(&write_values, kRed);
      ++red_pixel_count;
    }
    // Top-right quadrant
    else if (row < image_height / 2 && col >= image_width / 2) {
      AppendPixel(&write_values, kGreen);
      ++green_pixel_count;
    }
    // Bottom-right quadrant
    else if (row >= image_height / 2 && col >= image_width / 2) {
      AppendPixel(&write_values, kBlue);
      ++blue_pixel_count;
    }
    // Bottom-left quadrant
    else if (row >= image_height / 2 && col < image_width / 2) {
      AppendPixel(&write_values, kYellow);
      ++yellow_pixel_count;
    }
  }

  EXPECT_EQ(red_pixel_count, pixel_color_count);
  EXPECT_EQ(green_pixel_count, pixel_color_count);
  EXPECT_EQ(blue_pixel_count, pixel_color_count);
  EXPECT_EQ(yellow_pixel_count, pixel_color_count);

  WriteToSysmemBuffer(write_values, buffer_collection_info, 0, kBytesPerPixel, image_width,
                      image_height);

  GenerateImageForFlatlandInstance(0, child_session_, kChildRootTransform,
                                   std::move(ref_pair.import_token), {image_width, image_height},
                                   {0, 0}, 2, 2);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screenshotting!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);
  sc_args.set_size({render_target_width, render_target_height});
  sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_90_DEGREES);

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), write_values.size());

  // Compare read and write values for each quadrant.
  uint32_t top_left_correct = 0;
  uint32_t top_right_correct = 0;
  uint32_t bottom_right_correct = 0;
  uint32_t bottom_left_correct = 0;

  for (uint32_t i = 0; i < num_pixels_; ++i) {
    uint32_t row = i / render_target_width;
    uint32_t col = i % render_target_width;
    const uint8_t* read_value = &read_values[i * kBytesPerPixel];

    // Top-left quadrant
    if (row < render_target_height / 2 && col < render_target_width / 2) {
      if (PixelEquals(read_value, kYellow))
        top_left_correct++;
    }
    // Top-right quadrant
    else if (row < render_target_height / 2 && col >= render_target_width / 2) {
      if (PixelEquals(read_value, kRed))
        top_right_correct++;
    }
    // Bottom-right quadrant
    else if (row >= render_target_height / 2 && col >= render_target_width / 2) {
      if (PixelEquals(read_value, kGreen))
        bottom_right_correct++;
    }
    // Bottom-left quadrant
    else if (row >= render_target_height / 2 && col < render_target_width / 2) {
      if (PixelEquals(read_value, kBlue))
        bottom_left_correct++;
    }
  }

  EXPECT_EQ(top_left_correct, pixel_color_count);
  EXPECT_EQ(top_right_correct, pixel_color_count);
  EXPECT_EQ(bottom_right_correct, pixel_color_count);
  EXPECT_EQ(bottom_left_correct, pixel_color_count);
}

// Creates this image:
//          RRRRRGGGGG
//          RRRRRGGGGG
//          YYYYYBBBBB
//          YYYYYBBBBB
//
// Rotates into this image:
//          GGBB
//          GGBB
//          GGBB
//          GGBB
//          GGBB
//          RRYY
//          RRYY
//          RRYY
//          RRYY
//          RRYY
TEST_F(ScreenCaptureIntegrationTest, MultiColor270DegreeRotationScreenshot) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_height_;
  const uint32_t render_target_height = display_width_;

  // Create Buffer Collection for image#1 to add to scene graph.
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, image_width, image_height),
          std::move(ref_pair.export_token), RegisterBufferCollectionUsage::DEFAULT);

  // Write the image with the color scheme displayed in ASCII above.
  std::vector<uint8_t> write_values;

  uint32_t red_pixel_count = 0;
  uint32_t green_pixel_count = 0;
  uint32_t blue_pixel_count = 0;
  uint32_t yellow_pixel_count = 0;
  const uint32_t pixel_color_count = num_pixels_ / 4;

  for (uint32_t i = 0; i < num_pixels_; ++i) {
    uint32_t row = i / image_width;
    uint32_t col = i % image_width;

    // Top-left quadrant
    if (row < image_height / 2 && col < image_width / 2) {
      AppendPixel(&write_values, kRed);
      ++red_pixel_count;
    }
    // Top-right quadrant
    else if (row < image_height / 2 && col >= image_width / 2) {
      AppendPixel(&write_values, kGreen);
      ++green_pixel_count;
    }
    // Bottom-right quadrant
    else if (row >= image_height / 2 && col >= image_width / 2) {
      AppendPixel(&write_values, kBlue);
      ++blue_pixel_count;
    }
    // Bottom-left quadrant
    else if (row >= image_height / 2 && col < image_width / 2) {
      AppendPixel(&write_values, kYellow);
      ++yellow_pixel_count;
    }
  }

  EXPECT_EQ(red_pixel_count, pixel_color_count);
  EXPECT_EQ(green_pixel_count, pixel_color_count);
  EXPECT_EQ(blue_pixel_count, pixel_color_count);
  EXPECT_EQ(yellow_pixel_count, pixel_color_count);

  WriteToSysmemBuffer(write_values, buffer_collection_info, 0, kBytesPerPixel, image_width,
                      image_height);

  GenerateImageForFlatlandInstance(0, child_session_, kChildRootTransform,
                                   std::move(ref_pair.import_token), {image_width, image_height},
                                   {0, 0}, 2, 2);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screenshotting!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);
  sc_args.set_size({render_target_width, render_target_height});
  sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_270_DEGREES);

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), write_values.size());

  // Compare read and write values for each quadrant.
  uint32_t top_left_correct = 0;
  uint32_t top_right_correct = 0;
  uint32_t bottom_right_correct = 0;
  uint32_t bottom_left_correct = 0;

  for (uint32_t i = 0; i < num_pixels_; ++i) {
    uint32_t row = i / render_target_width;
    uint32_t col = i % render_target_width;
    const uint8_t* read_value = &read_values[i * kBytesPerPixel];

    // Top-left quadrant
    if (row < render_target_height / 2 && col < render_target_width / 2) {
      if (PixelEquals(read_value, kGreen))
        top_left_correct++;
    }
    // Top-right quadrant
    else if (row < render_target_height / 2 && col >= render_target_width / 2) {
      if (PixelEquals(read_value, kBlue))
        top_right_correct++;
    }
    // Bottom-right quadrant
    else if (row >= render_target_height / 2 && col >= render_target_width / 2) {
      if (PixelEquals(read_value, kYellow))
        bottom_right_correct++;
    }
    // Bottom-left quadrant
    else if (row >= render_target_height / 2 && col < render_target_width / 2) {
      if (PixelEquals(read_value, kRed))
        bottom_left_correct++;
    }
  }

  EXPECT_EQ(top_left_correct, pixel_color_count);
  EXPECT_EQ(top_right_correct, pixel_color_count);
  EXPECT_EQ(bottom_right_correct, pixel_color_count);
  EXPECT_EQ(bottom_left_correct, pixel_color_count);
}

TEST_F(ScreenCaptureIntegrationTest, FilledRectScreenshot) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_width_;
  const uint32_t render_target_height = display_height_;

  const ContentId kFilledRectId = {1};
  const TransformId kTransformId = {2};

  // Create a fuchsia colored rectangle.
  child_session_->CreateFilledRect(kFilledRectId);
  child_session_->SetSolidFill(kFilledRectId, {1, 0, 1, 1}, {image_width, image_height});

  // Associate the rect with a transform.
  child_session_->CreateTransform(kTransformId);
  child_session_->SetContent(kTransformId, kFilledRectId);

  // Attach the transform to the scene
  child_session_->AddChild(kChildRootTransform, kTransformId);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screencapturing!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/1, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_size({render_target_width, render_target_height});
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), num_pixels_ * kBytesPerPixel);

  // Compare read and write values.
  uint32_t num_fuchsia_count = 0;
  static constexpr uint8_t kFuchsia[] = {255, 0, 255, 255};

  for (size_t i = 0; i < read_values.size(); i += kBytesPerPixel) {
    if (PixelEquals(&read_values[i], kFuchsia))
      num_fuchsia_count++;
  }

  EXPECT_EQ(num_fuchsia_count, num_pixels_);
}

TEST_F(ScreenCaptureIntegrationTest, ChangeFilledRectScreenshots) {
  const uint32_t image_width = display_width_;
  const uint32_t image_height = display_height_;
  const uint32_t render_target_width = display_width_;
  const uint32_t render_target_height = display_height_;

  const ContentId kFilledRectId = {1};
  const TransformId kTransformId = {2};

  // Create a red rectangle.
  child_session_->CreateFilledRect(kFilledRectId);
  // Set as RGBA. Corresponds to kRed.
  child_session_->SetSolidFill(kFilledRectId, {1, 0, 0, 1}, {image_width, image_height});

  // Associate the rect with a transform.
  child_session_->CreateTransform(kTransformId);
  child_session_->SetContent(kTransformId, kFilledRectId);

  // Attach the transform to the scene
  child_session_->AddChild(kChildRootTransform, kTransformId);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screencapturing!

  // Create buffer collection to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens scr_ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info =
      CreateBufferCollectionInfoWithConstraints(
          utils::CreateDefaultConstraints(/*buffer_count=*/2, render_target_width,
                                          render_target_height),
          std::move(scr_ref_pair.export_token), RegisterBufferCollectionUsage::SCREENSHOT);

  // Configure buffers in ScreenCapture client.
  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(scr_ref_pair.import_token));
  sc_args.set_size({render_target_width, render_target_height});
  sc_args.set_buffer_count(sc_buffer_collection_info.buffer_count);

  bool alloc_result = false;
  screen_capture_->Configure(std::move(sc_args),
                             [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                               EXPECT_FALSE(result.is_error());
                               alloc_result = true;
                             });

  RunLoopUntil([&alloc_result] { return alloc_result; });

  // Take Screenshot!
  const auto& cs_result = CaptureScreen(screen_capture_);
  const auto& read_values =
      ExtractScreenCapture(cs_result, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values.size(), num_pixels_ * kBytesPerPixel);

  // Compare read and write values.
  uint32_t num_red_count = 0;

  for (size_t i = 0; i < read_values.size(); i += kBytesPerPixel) {
    if (PixelEquals(&read_values[i], kRed))
      num_red_count++;
  }

  EXPECT_EQ(num_red_count, num_pixels_);

  // Now change the color of the screen.

  const ContentId kFilledRectId2 = {2};
  const TransformId kTransformId2 = {3};

  // Create a blue rectangle.
  child_session_->CreateFilledRect(kFilledRectId2);
  // Set as RGBA. Corresponds to kBlue.
  child_session_->SetSolidFill(kFilledRectId2, {0, 0, 1, 1}, {image_width, image_height});

  // Associate the rect with a transform.
  child_session_->CreateTransform(kTransformId2);
  child_session_->SetContent(kTransformId2, kFilledRectId2);

  // Attach the transform to the scene
  child_session_->AddChild(kChildRootTransform, kTransformId2);
  BlockingPresent(child_session_);

  // The scene graph is now ready for screencapturing!

  // Take Screenshot!
  const auto& cs_result2 = CaptureScreen(screen_capture_);
  const auto& read_values2 =
      ExtractScreenCapture(cs_result2, sc_buffer_collection_info, kBytesPerPixel,
                           render_target_width, render_target_height);

  EXPECT_EQ(read_values2.size(), num_pixels_ * kBytesPerPixel);

  // Compare read and write values.
  uint32_t num_blue_count = 0;

  for (size_t i = 0; i < read_values2.size(); i += kBytesPerPixel) {
    if (PixelEquals(&read_values2[i], kBlue))
      num_blue_count++;
  }

  EXPECT_EQ(num_blue_count, num_pixels_);
}

}  // namespace integration_tests
