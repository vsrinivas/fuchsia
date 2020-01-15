// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d-on-device-test.h"

#include <lib/fit/function.h>
#include <lib/image-format/image_format.h>
#include <lib/sync/completion.h>

#include <vector>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

#include "src/camera/drivers/hw_accel/ge2d/ge2d-regs.h"
#include "src/camera/drivers/hw_accel/ge2d/ge2d.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace ge2d {

namespace {

Ge2dDevice* g_ge2d_device;

constexpr uint32_t kImageFormatTableSize = 3;
constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 1024;

class Ge2dDeviceTest : public zxtest::Test {
 public:
  void TearDown() override {
    EXPECT_OK(camera::DestroyContiguousBufferCollection(input_buffer_collection_));
    EXPECT_OK(camera::DestroyContiguousBufferCollection(output_buffer_collection_));
  }

  void SetupInput() {
    uint32_t buffer_collection_count = 2;
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[0],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[1],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth / 2, kHeight / 2));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[2],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth / 4, kHeight / 4));
    // Set up fake Resize info
    resize_info_.crop.x = 0;
    resize_info_.crop.y = 0;
    resize_info_.crop.width = kWidth;
    resize_info_.crop.height = kHeight;
    resize_info_.output_rotation = GE2D_ROTATION_ROTATION_0;

    zx_status_t status = camera::CreateContiguousBufferCollectionInfo(
        input_buffer_collection_, output_image_format_table_[0], g_ge2d_device->bti().get(),
        buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo(
        output_buffer_collection_, output_image_format_table_[0], g_ge2d_device->bti().get(),
        buffer_collection_count);
    ASSERT_OK(status);
    for (uint32_t i = 0; i < output_buffer_collection_.buffer_count; i++) {
      size_t size;
      ASSERT_OK(zx_vmo_get_size(output_buffer_collection_.buffers[i].vmo, &size));
      ASSERT_OK(zx_vmo_op_range(output_buffer_collection_.buffers[i].vmo, ZX_VMO_OP_CACHE_CLEAN, 0,
                                size, nullptr, 0));
    }
  }

  void SetupCallbacks() {
    res_callback_.frame_resolution_changed = [](void* ctx, const frame_available_info* info) {
      EXPECT_TRUE(false);
    };
    res_callback_.ctx = this;

    remove_task_callback_.task_removed = [](void* ctx, task_remove_status_t status) {
      EXPECT_TRUE(false);
    };
    remove_task_callback_.ctx = this;
    frame_callback_.frame_ready = [](void* ctx, const frame_available_info* info) {
      static_cast<Ge2dDeviceTest*>(ctx)->RunFrameCallback(info);
    };
    frame_callback_.ctx = this;
  }

  void SetFrameCallback(fit::callback<void(const frame_available_info* info)> callback) {
    client_frame_callback_ = std::move(callback);
  }

  void RunFrameCallback(const frame_available_info* info) { client_frame_callback_(info); }

 protected:
  void CompareCroppedOutput(const frame_available_info* info);

  hw_accel_frame_callback_t frame_callback_;
  hw_accel_res_change_callback_t res_callback_;
  hw_accel_remove_task_callback_t remove_task_callback_;

  fit::callback<void(const frame_available_info* info)> client_frame_callback_;
  buffer_collection_info_2_t input_buffer_collection_;
  buffer_collection_info_2_t output_buffer_collection_;
  image_format_2_t output_image_format_table_[kImageFormatTableSize];
  sync_completion_t completion_;
  resize_info_t resize_info_;
};

void WriteDataToVmo(zx_handle_t vmo, const image_format_2_t& format) {
  uint32_t size = format.bytes_per_row * format.coded_height * 3 / 2;
  std::vector<uint8_t> input_data(size);
  constexpr uint32_t kRunLength = 230;
  // 230 should not by a divisor of the stride or height, to ensure adjacent lines have different
  // contents.
  static_assert(kWidth % kRunLength != 0, "kRunLength is a bad choice");

  for (uint32_t i = 0; i < size; i++) {
    input_data[i] = i % kRunLength;
  }
  ASSERT_OK(zx_vmo_write(vmo, input_data.data(), 0, size));
  ASSERT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0));
}

// Compare a rectangular region of |vmo_a| with a rectangular region of the same size of |vmo_b|.
void CheckSubPlaneEqual(zx_handle_t vmo_a, zx_handle_t vmo_b, uint32_t offset_a, uint32_t offset_b,
                        uint32_t stride_a, uint32_t stride_b, uint32_t width, uint32_t height,
                        const char* error_type) {
  std::vector<uint8_t> row_data_a(width);
  std::vector<uint8_t> row_data_b(width);
  uint32_t error_count = 0;
  for (uint32_t y = 0; y < height; y++) {
    ASSERT_OK(zx_vmo_read(vmo_a, row_data_a.data(), stride_a * y + offset_a, width));
    ASSERT_OK(zx_vmo_read(vmo_b, row_data_b.data(), stride_b * y + offset_b, width));
    for (uint32_t x = 0; x < width; x++) {
      EXPECT_EQ(row_data_a[x], row_data_b[x], "%s at (%d, %d)", error_type, x, y);
      if (row_data_a[x] != row_data_b[x]) {
        if (++error_count >= 10)
          return;
      }
    }
  }
}

void CacheInvalidateVmo(zx_handle_t vmo) {
  uint64_t size;
  ASSERT_OK(zx_vmo_get_size(vmo, &size));
  ASSERT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, size, nullptr, 0));
}

void CheckEqual(zx_handle_t vmo_a, zx_handle_t vmo_b, const image_format_2_t& format) {
  CacheInvalidateVmo(vmo_a);
  CacheInvalidateVmo(vmo_b);
  CheckSubPlaneEqual(vmo_a, vmo_b, 0, 0, format.bytes_per_row, format.bytes_per_row,
                     format.coded_width, format.coded_height, "Y");
  uint32_t uv_offset = format.bytes_per_row * format.coded_height;
  CheckSubPlaneEqual(vmo_a, vmo_b, uv_offset, uv_offset, format.bytes_per_row, format.bytes_per_row,
                     format.coded_width, format.coded_height / 2, "UV");
}

void Ge2dDeviceTest::CompareCroppedOutput(const frame_available_info* info) {
  fprintf(stderr, "Got frame_ready, id %d\n", info->buffer_id);
  zx_handle_t vmo_a = input_buffer_collection_.buffers[0].vmo;
  zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
  image_format_2_t input_format = output_image_format_table_[0];
  image_format_2_t output_format = output_image_format_table_[1];

  CacheInvalidateVmo(vmo_a);
  CacheInvalidateVmo(vmo_b);
  uint32_t a_start_offset = input_format.bytes_per_row * resize_info_.crop.y + resize_info_.crop.x;
  CheckSubPlaneEqual(vmo_a, vmo_b, a_start_offset, 0, input_format.bytes_per_row,
                     output_format.bytes_per_row, output_format.coded_width,
                     output_format.coded_height, "Y");
  // When scaling is a disabled we currently repeat the input U and V data instead of
  // interpolating, so the output UV should just be a shifted version of the input.
  uint32_t a_uv_offset = input_format.bytes_per_row * input_format.coded_height;
  uint32_t a_uv_start_offset = a_uv_offset +
                               input_format.bytes_per_row * (resize_info_.crop.y / 2) +
                               (resize_info_.crop.x / 2) * 2;
  uint32_t b_uv_offset = output_format.bytes_per_row * output_format.coded_height;
  CheckSubPlaneEqual(vmo_a, vmo_b, a_uv_start_offset, b_uv_offset, input_format.bytes_per_row,
                     output_format.bytes_per_row, output_format.coded_width,
                     output_format.coded_height / 2, "UV");
}

TEST_F(Ge2dDeviceTest, SameSize) {
  SetupCallbacks();
  SetupInput();

  WriteDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    fprintf(stderr, "Got frame_ready, id %d\n", info->buffer_id);
    CheckEqual(input_buffer_collection_.buffers[0].vmo,
               output_buffer_collection_.buffers[info->buffer_id].vmo,
               output_image_format_table_[0]);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = g_ge2d_device->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = g_ge2d_device->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

TEST_F(Ge2dDeviceTest, Crop) {
  SetupCallbacks();
  SetupInput();

  WriteDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = kWidth / 2;
  resize_info_.crop.y = kHeight / 2;
  resize_info_.crop.width = kWidth / 2;
  resize_info_.crop.height = kHeight / 2;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = g_ge2d_device->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 1,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = g_ge2d_device->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

TEST_F(Ge2dDeviceTest, CropOddOffset) {
  SetupCallbacks();
  SetupInput();

  WriteDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 1;
  resize_info_.crop.y = 1;
  resize_info_.crop.width = kWidth / 2;
  resize_info_.crop.height = kHeight / 2;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = g_ge2d_device->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 1,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = g_ge2d_device->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

}  // namespace

zx_status_t Ge2dDeviceTester::RunTests(Ge2dDevice* ge2d) {
  g_ge2d_device = ge2d;
  const int kArgc = 1;
  const char* argv[kArgc] = {"ge2d-test"};
  return RUN_ALL_TESTS(kArgc, const_cast<char**>(argv));
}

}  // namespace ge2d
