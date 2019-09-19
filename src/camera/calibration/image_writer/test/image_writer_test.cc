// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../image_writer.h"

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/camera/drivers/isp/modules/dma-format.h"

namespace camera {
namespace {

constexpr uint32_t kWidth = 50;
constexpr uint32_t kHeight = 50;
constexpr camera::DmaFormat::PixelType kPixelTypeRaw12 = camera::DmaFormat::RAW12;

TEST(ImageWriterTest, Constructor) { ImageWriter::Create(kWidth, kHeight, kPixelTypeRaw12); }
TEST(ImageWriterTest, ConstructorInvalidPixelType) {
  ASSERT_DEATH([] { ImageWriter::Create(kWidth, kHeight, camera::DmaFormat::INVALID); });
}

class ImageWriterTest : public zxtest::Test {
 public:
  // TODO(nzo): use SetUp().

 protected:
  std::unique_ptr<ImageWriter> image_writer_;
};

TEST_F(ImageWriterTest, CreateImage) {
  std::unique_ptr<ImageWriter> image_writer_ =
      ImageWriter::Create(kWidth, kHeight, kPixelTypeRaw12);
  const size_t num_pixels = image_writer_->VmoSize() / 4;

  zx_status_t status;
  zx::vmo vmo;

  status = image_writer_->CreateImage(&vmo);
  EXPECT_OK(status, "image created successfully");

  auto buf = std::vector<uint32_t>(num_pixels, 0);
  vmo.read(&buf.front(), 0, buf.size());

  auto expected_buf = std::vector<uint32_t>(num_pixels, 1);

  EXPECT_BYTES_EQ(&buf.front(), &expected_buf.front(), expected_buf.size(),
                  "read buffer filled correctly");
}

}  // namespace
}  // namespace camera
