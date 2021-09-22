// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../screenshot.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using allocation::BufferCollectionImporter;
using fuchsia::ui::composition::CreateImageArgs;
using fuchsia::ui::composition::ScreenshotError;
using testing::_;

namespace screenshot {
namespace test {

class ScreenshotTest : public gtest::TestLoopFixture {
 public:
  ScreenshotTest() {}
  void SetUp() override {
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr();

    mock_buffer_collection_importer_ = new allocation::MockBufferCollectionImporter();
    buffer_collection_importer_ =
        std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer_);

    // Capture uninteresting cleanup calls from Allocator dtor.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_))
        .Times(::testing::AtLeast(0));
  }

  void TearDown() override { RunLoopUntilIdle(); }

 protected:
  allocation::MockBufferCollectionImporter* mock_buffer_collection_importer_;
  std::shared_ptr<allocation::BufferCollectionImporter> buffer_collection_importer_;

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

std::pair<const std::vector<Rectangle2D>&, const std::vector<allocation::ImageMetadata>&>
GetRenderables() {
  return std::make_pair<const std::vector<Rectangle2D>&,
                        const std::vector<allocation::ImageMetadata>&>({}, {});
}

TEST_F(ScreenshotTest, CreateImage_SingleImporter_Success) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
  screenshot_importers.push_back(buffer_collection_importer_);
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, screenshot_importers, nullptr,
                            &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(15122);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(true));

  bool created_image = false;
  sc.CreateImage(std::move(args),
                 [&created_image](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_FALSE(result.is_err());
                   created_image = true;
                 });
  RunLoopUntilIdle();
  EXPECT_TRUE(created_image);
}

TEST_F(ScreenshotTest, CreateImage_SingleImporter_Failure) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
  screenshot_importers.push_back(buffer_collection_importer_);
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, screenshot_importers, nullptr,
                            &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(15122);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(false));

  ScreenshotError error;
  sc.CreateImage(std::move(args),
                 [&error](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_TRUE(result.is_err());
                   error = result.err();
                 });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenshotError::BAD_OPERATION);
}

TEST_F(ScreenshotTest, CreateImage_MultipleImporters_Success) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;

  auto mock_buffer_collection_importer2 = new allocation::MockBufferCollectionImporter();
  auto buffer_collection_importer2 =
      std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer2);

  screenshot_importers.push_back(buffer_collection_importer_);
  screenshot_importers.push_back(buffer_collection_importer2);
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, screenshot_importers, nullptr,
                            &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(15122);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_buffer_collection_importer2, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(true));

  bool created_image = false;
  sc.CreateImage(std::move(args),
                 [&created_image](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_FALSE(result.is_err());
                   created_image = true;
                 });
  RunLoopUntilIdle();
  EXPECT_TRUE(created_image);
}

TEST_F(ScreenshotTest, CreateImage_MultipleImporters_ImportFailure) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;

  auto mock_buffer_collection_importer2 = new allocation::MockBufferCollectionImporter();
  auto buffer_collection_importer2 =
      std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer2);

  screenshot_importers.push_back(buffer_collection_importer_);
  screenshot_importers.push_back(buffer_collection_importer2);
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, screenshot_importers, nullptr,
                            &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(15122);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_buffer_collection_importer2, ImportBufferImage(_))
      .WillRepeatedly(testing::Return(false));

  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_));

  ScreenshotError error;
  sc.CreateImage(std::move(args),
                 [&error](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_TRUE(result.is_err());
                   error = result.err();
                 });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenshotError::BAD_OPERATION);
}

TEST_F(ScreenshotTest, CreateImage_MissingArguments) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, {}, nullptr, &GetRenderables);
  sc.CreateImage({}, [](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
    EXPECT_TRUE(result.is_err());
    auto error = result.err();
    EXPECT_EQ(error, ScreenshotError::MISSING_ARGS);
  });
}

TEST_F(ScreenshotTest, CreateImage_InvalidID) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, {}, nullptr, &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(0);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  ScreenshotError error;
  sc.CreateImage(std::move(args),
                 [&error](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_TRUE(result.is_err());
                   error = result.err();
                 });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenshotError::BAD_OPERATION);
}

TEST_F(ScreenshotTest, CreateImage_DuplicateID) {
  fuchsia::ui::composition::ScreenshotPtr screenshotter;
  screenshot::Screenshot sc(screenshotter.NewRequest(), 100, 100, {}, nullptr, &GetRenderables);

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  CreateImageArgs args;
  args.set_image_id(15410);
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_vmo_index(1);
  args.set_size({1, 1});

  sc.CreateImage(std::move(args),
                 [](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_FALSE(result.is_err());
                 });

  CreateImageArgs args2;
  args2.set_image_id(15410);
  args2.set_import_token(std::move(ref_pair.import_token));
  args2.set_vmo_index(1);
  args2.set_size({1, 1});

  ScreenshotError error;

  sc.CreateImage(std::move(args),
                 [&error](fuchsia::ui::composition::Screenshot_CreateImage_Result result) {
                   EXPECT_TRUE(result.is_err());
                   error = result.err();
                 });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenshotError::BAD_OPERATION);
}

}  // namespace test
}  // namespace screenshot
