// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/hw_accel/ge2d/test/ge2d-on-device-test.h"

#include <lib/driver-unit-test/utils.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/image-format/image_format.h>
#include <lib/sync/completion.h>

#include <memory>
#include <vector>

#include <ddk/debug.h>
#include <zxtest/zxtest.h>

#include "src/camera/drivers/hw_accel/ge2d/ge2d-regs.h"
#include "src/camera/drivers/hw_accel/ge2d/ge2d.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace ge2d {

namespace {

constexpr uint32_t kImageFormatTableSize = 3;
constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 1024;

constexpr uint32_t kWatermarkVerticalOffset = 10;
constexpr uint32_t kWatermarkHorizontalOffset = 10;
constexpr uint32_t kWatermarkWidth = 65;
constexpr uint32_t kWatermarkHeight = 64;

class Ge2dDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    zx_status_t status = ge2d::Ge2dDevice::Setup(driver_unit_test::GetParent(), &ge2d_device_);
    ZX_ASSERT(status == ZX_OK);
  }
  void TearDown() override {
    EXPECT_OK(camera::DestroyContiguousBufferCollection(input_buffer_collection_));
    EXPECT_OK(camera::DestroyContiguousBufferCollection(output_buffer_collection_));
  }

  void SetupInput(uint32_t input_format = fuchsia_sysmem_PixelFormatType_NV12,
                  uint32_t output_format = fuchsia_sysmem_PixelFormatType_NV12) {
    uint32_t buffer_collection_count = 2;
    ASSERT_OK(
        camera::GetImageFormat(output_image_format_table_[0], output_format, kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[1], output_format, kWidth / 2,
                                     kHeight / 2));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[2], output_format, kWidth / 4,
                                     kHeight / 4));
    // Set up fake Resize info
    resize_info_.crop.x = 0;
    resize_info_.crop.y = 0;
    resize_info_.crop.width = kWidth;
    resize_info_.crop.height = kHeight;
    resize_info_.output_rotation = GE2D_ROTATION_ROTATION_0;

    image_format_2_t input_image_format;
    ASSERT_OK(camera::GetImageFormat(input_image_format, input_format, kWidth, kHeight));

    zx_status_t status = camera::CreateContiguousBufferCollectionInfo(
        input_buffer_collection_, input_image_format, ge2d_device_->bti().get(),
        buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo(
        output_buffer_collection_, output_image_format_table_[0], ge2d_device_->bti().get(),
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
      static_cast<Ge2dDeviceTest*>(ctx)->RunResolutionChangedCallback(info);
    };
    res_callback_.ctx = this;

    remove_task_callback_.task_removed = [](void* ctx, task_remove_status_t status) {
      static_cast<Ge2dDeviceTest*>(ctx)->RunTaskRemovedCallback(status);
    };
    remove_task_callback_.ctx = this;
    frame_callback_.frame_ready = [](void* ctx, const frame_available_info* info) {
      static_cast<Ge2dDeviceTest*>(ctx)->RunFrameCallback(info);
    };
    frame_callback_.ctx = this;
  }

  void SetFrameCallback(fit::function<void(const frame_available_info* info)> callback) {
    client_frame_callback_ = std::move(callback);
  }

  void RunFrameCallback(const frame_available_info* info) { client_frame_callback_(info); }

  void SetTaskRemovedCallback(fit::function<void(task_remove_status_t status)> callback) {
    task_removed_callback_ = std::move(callback);
  }

  void RunTaskRemovedCallback(task_remove_status_t status) { task_removed_callback_(status); }

  void RunResolutionChangedCallback(const frame_available_info* info) {
    resolution_changed_callback_(info);
  }
  void SetResolutionChangedCallback(
      fit::function<void(const frame_available_info* info)> callback) {
    resolution_changed_callback_ = std::move(callback);
  }

 protected:
  void CompareCroppedOutput(const frame_available_info* info, float tolerance = 0.0f);

  void SetupWatermarkInfo() {
    image_format_2_t watermark_image_format;
    ASSERT_OK(camera::GetImageFormat(watermark_image_format,
                                     fuchsia_sysmem_PixelFormatType_R8G8B8A8, kWatermarkWidth,
                                     kWatermarkHeight));

    watermark_info_.wm_image_format = watermark_image_format;
    watermark_info_.loc_x = kWatermarkHorizontalOffset;
    watermark_info_.loc_y = kWatermarkVerticalOffset;
    watermark_info_.global_alpha = 1.f;
  }

  zx::vmo CreateWatermarkVmo() {
    zx::vmo watermark_vmo;
    EXPECT_OK(zx::vmo::create(watermark_info_.wm_image_format.bytes_per_row *
                                  watermark_info_.wm_image_format.coded_height,
                              0, &watermark_vmo));
    return watermark_vmo;
  }

  std::unique_ptr<Ge2dDevice> ge2d_device_;
  hw_accel_frame_callback_t frame_callback_;
  hw_accel_res_change_callback_t res_callback_;
  hw_accel_remove_task_callback_t remove_task_callback_;

  fit::function<void(const frame_available_info* info)> client_frame_callback_;
  fit::function<void(task_remove_status_t status)> task_removed_callback_;
  fit::function<void(const frame_available_info* info)> resolution_changed_callback_;
  buffer_collection_info_2_t input_buffer_collection_;
  buffer_collection_info_2_t output_buffer_collection_;
  image_format_2_t output_image_format_table_[kImageFormatTableSize];
  sync_completion_t completion_;
  resize_info_t resize_info_;
  water_mark_info_t watermark_info_;
  uint32_t input_format_index_ = 0;
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

void WriteConstantColorToVmo(zx_handle_t vmo, uint32_t y, uint32_t u, uint32_t v,
                             const image_format_2_t& format) {
  uint32_t size = format.bytes_per_row * format.coded_height * 3 / 2;
  std::vector<uint8_t> input_data(format.coded_width);
  memset(input_data.data(), y, input_data.size());
  uint32_t offset = 0;
  for (uint32_t y = 0; y < format.coded_height; ++y) {
    ASSERT_OK(zx_vmo_write(vmo, input_data.data(), offset, input_data.size()));
    offset += format.bytes_per_row;
  }
  for (uint32_t x = 0; x < format.coded_width / 2; ++x) {
    input_data[x * 2] = u;
    input_data[x * 2 + 1] = v;
  }
  for (uint32_t y = 0; y < format.coded_height / 2; ++y) {
    ASSERT_OK(zx_vmo_write(vmo, input_data.data(), offset, input_data.size()));
    offset += format.bytes_per_row;
  }
  ASSERT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0));
}

void WriteConstantRgbaToVmo(zx_handle_t vmo, uint32_t r, uint32_t g, uint32_t b, uint32_t a,
                            const image_format_2_t& format) {
  uint32_t size = format.bytes_per_row * format.coded_height;
  std::vector<uint8_t> input_data(format.coded_width * 4);
  for (uint32_t x = 0; x < format.coded_width; x++) {
    input_data[4 * x] = r;
    input_data[4 * x + 1] = g;
    input_data[4 * x + 2] = b;
    input_data[4 * x + 3] = a;
  }
  uint32_t offset = 0;
  for (uint32_t y = 0; y < format.coded_height; ++y) {
    ASSERT_OK(zx_vmo_write(vmo, input_data.data(), offset, input_data.size()));
    offset += format.bytes_per_row;
  }
  ASSERT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0));
}

// Write out data without large jumps so it's easier to do a comparison using a
// tolerance.
void WriteScalingDataToVmo(zx_handle_t vmo, const image_format_2_t& format) {
  std::vector<uint8_t> input_data(format.coded_width);
  // Write to both Y and UV planes in this loop.
  for (uint32_t y = 0; y < format.coded_height * 3 / 2; y++) {
    for (uint32_t x = 0; x < format.coded_width; ++x) {
      // Multiply by 2 so we can see interpolated values in the output.
      uint32_t start_val = 2 * x + 4 * y;
      // Ensure U and V values are very different, because we don't want to mix
      // them up.
      if (y >= format.coded_height && ((x & 1) == 1)) {
        start_val += 63;
      }
      // Limit result to [0..255]
      constexpr uint32_t kMaxPlus1 = 256;
      // Output should go 0-255,255-0,0-255 etc. This is a smooth function so there aren't large
      // jumps in output that could cause pixels near the jump to be outside the tolerance.
      start_val %= kMaxPlus1 * 2;
      if (start_val >= kMaxPlus1)
        start_val = (kMaxPlus1 * 2 - 1) - start_val;
      input_data[x] = start_val;
    }
    ASSERT_OK(zx_vmo_write(vmo, input_data.data(), y * format.bytes_per_row, format.coded_width));
  }
  uint32_t size = format.bytes_per_row * format.coded_height * 3 / 2;
  ASSERT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0));
}

float lerp(float x, float y, float a) { return x + (y - x) * a; }

template <typename T>
float BilinearInterp(T load, float x, float y) {
  x = std::max(0.0f, x);
  y = std::max(0.0f, y);
  int low_x = x;
  int low_y = y;
  // If the input is on a pixel center then read from that both times, to avoid
  // reading out of bounds.
  int upper_x = low_x == x ? low_x : low_x + 1;
  int upper_y = low_y == y ? low_y : low_y + 1;
  float a_0_0 = load(low_x, low_y);
  float a_0_1 = load(low_x, upper_y);
  float a_1_0 = load(upper_x, low_y);
  float a_1_1 = load(upper_x, upper_y);
  float row_1 = lerp(a_0_0, a_1_0, x - low_x);
  float row_2 = lerp(a_0_1, a_1_1, x - low_x);
  return lerp(row_1, row_2, y - low_y);
}

// Compare a rectangular region of |addr_a| with a rectangular region of |addr_b|.
void CheckSubPlaneEqual(void* addr_a, void* addr_b, uint32_t offset_a, uint32_t offset_b,
                        uint32_t stride_a, uint32_t stride_b, uint32_t width,
                        uint32_t bytes_per_pixel, uint32_t height, float x_scale, float y_scale,
                        float tolerance, const char* error_type) {
  uint32_t error_count = 0;
  uint8_t* region_start_a = reinterpret_cast<uint8_t*>(addr_a) + offset_a;
  uint8_t* region_start_b = reinterpret_cast<uint8_t*>(addr_b) + offset_b;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width / bytes_per_pixel; x++) {
      uint8_t output_value[4] = {};
      memcpy(&output_value, region_start_b + stride_b * y + x * bytes_per_pixel, bytes_per_pixel);
      constexpr float kHalfPixel = 0.5f;
      // Add and subtract half a pixel to try to account for the pixel center
      // location.
      float input_y = ((y + kHalfPixel) * y_scale - kHalfPixel);
      float input_x = ((x + kHalfPixel) * x_scale - kHalfPixel);
      for (uint32_t c = 0; c < bytes_per_pixel; c++) {
        float input_value = BilinearInterp(
            [&](uint32_t x, uint32_t y) {
              return (region_start_a + stride_a * y + x * bytes_per_pixel)[c];
            },
            input_x, input_y);
        EXPECT_GE(tolerance, std::abs(output_value[c] - input_value),
                  "%s component %d input %f vs output %d at output (%d, %d), input (%f, %f)",
                  error_type, c, input_value, output_value[c], x, y, input_x, input_y);
        if (tolerance < std::abs(output_value[c] - input_value)) {
          if (++error_count >= 10)
            return;
        }
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
  fzl::VmoMapper mapper_a;
  ASSERT_OK(mapper_a.Map(*zx::unowned_vmo(vmo_a), 0, 0, ZX_VM_PERM_READ));
  fzl::VmoMapper mapper_b;
  ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));
  CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), 0, 0, format.bytes_per_row,
                     format.bytes_per_row, format.coded_width, 1, format.coded_height, 1.0f, 1.0f,
                     0, "Y");
  uint32_t uv_offset = format.bytes_per_row * format.coded_height;
  CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), uv_offset, uv_offset, format.bytes_per_row,
                     format.bytes_per_row, format.coded_width, 2, format.coded_height / 2, 1.0f,
                     1.0f, 0, "UV");
}

uint8_t* GetPointerToPixel(void* base, const image_format_2_t& format, uint32_t x, uint32_t y,
                           uint32_t plane = 0) {
  if (format.pixel_format.type == fuchsia_sysmem_PixelFormatType_NV12) {
    if (plane == 0) {
      return static_cast<uint8_t*>(base) + format.bytes_per_row * y + x;
    } else {
      return static_cast<uint8_t*>(base) + format.bytes_per_row * (format.coded_height + y / 2) +
             (x / 2) * 2;
    }
  } else {
    return static_cast<uint8_t*>(base) + format.bytes_per_row * y + 4 * x;
  }
}

// Check that a region is a YUV solid color
void CheckYUVRegion(void* data, const image_format_2_t& format, uint8_t y_component, uint8_t u,
                    uint8_t v, uint32_t x_start, uint32_t width, uint32_t y_start,
                    uint32_t height) {
  uint32_t error_count = 0;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t value = *GetPointerToPixel(data, format, x_start + x, y_start + y, 0);
      EXPECT_EQ(y_component, value, "(%d, %d)", x, y);
      if (y_component != value) {
        error_count++;
      }
      if (error_count > 10)
        return;
    }
  }
  for (uint32_t y = 0; y < height / 2; y++) {
    for (uint32_t x = 0; x < width / 2; x++) {
      uint32_t uv_value = *reinterpret_cast<volatile uint16_t*>(
          GetPointerToPixel(data, format, x_start + x, y_start + y, 1));
      EXPECT_EQ(u, uv_value & 0xff, "(%d, %d)", x, y);
      EXPECT_EQ(v, uv_value >> 8, "(%d, %d)", x, y);
      if (u != (uv_value & 0xff) || (v != uv_value >> 8)) {
        error_count++;
      }
      if (error_count > 10)
        return;
    }
  }
}

void Ge2dDeviceTest::CompareCroppedOutput(const frame_available_info* info, float tolerance) {
  fprintf(stderr, "Got frame_ready, id %d\n", info->buffer_id);
  zx_handle_t vmo_a = input_buffer_collection_.buffers[0].vmo;
  zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
  image_format_2_t input_format = output_image_format_table_[input_format_index_];
  image_format_2_t output_format = output_image_format_table_[info->metadata.image_format_index];

  CacheInvalidateVmo(vmo_a);
  CacheInvalidateVmo(vmo_b);

  fzl::VmoMapper mapper_a;
  ASSERT_OK(mapper_a.Map(*zx::unowned_vmo(vmo_a), 0, 0, ZX_VM_PERM_READ));
  fzl::VmoMapper mapper_b;
  ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));

  uint32_t a_start_offset = input_format.bytes_per_row * resize_info_.crop.y + resize_info_.crop.x;
  float width_scale = static_cast<float>(resize_info_.crop.width) / output_format.coded_width;
  float height_scale = static_cast<float>(resize_info_.crop.height) / output_format.coded_height;
  // Account for rounding and other minor issues.
  if (width_scale != 1.0f || height_scale != 1.0f)
    tolerance += 0.7;
  // Pre-scaler may cause minor changes.
  if (width_scale > 2.0f)
    tolerance += 1;
  if (height_scale > 2.0f)
    tolerance += 2;
  uint32_t height_to_check = output_format.coded_height;
  if (height_scale < 1.0f) {
    // Last row may be blended with default color, due to the fact that its pixel center
    // location is greater than the largest input pixel center location.
    height_to_check--;
  }
  uint32_t width_to_check = output_format.coded_height;
  if (width_scale < 1.0f) {
    // Same as height above.
    width_to_check--;
  }
  CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), a_start_offset, 0,
                     input_format.bytes_per_row, output_format.bytes_per_row, width_to_check, 1,
                     height_to_check, width_scale, height_scale, tolerance, "Y");
  // When scaling is a disabled we currently repeat the input U and V data instead of
  // interpolating, so the output UV should just be a shifted version of the input.
  uint32_t a_uv_offset = input_format.bytes_per_row * input_format.coded_height;
  uint32_t a_uv_start_offset = a_uv_offset +
                               input_format.bytes_per_row * (resize_info_.crop.y / 2) +
                               (resize_info_.crop.x / 2) * 2;
  uint32_t b_uv_offset = output_format.bytes_per_row * output_format.coded_height;

  // Because subsampling reduces the precision of everything, we need to
  // increase the tolerance here.
  if (tolerance > 0)
    tolerance = tolerance * 2 + 1;
  if (width_scale < 1.0f || height_scale < 1.0f)
    tolerance += 2.0f;
  CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), a_uv_start_offset, b_uv_offset,
                     input_format.bytes_per_row, output_format.bytes_per_row, width_to_check, 2,
                     height_to_check / 2, width_scale, height_scale, tolerance, "UV");
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
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
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
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 1,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
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
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 1,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Scale width down to 50%, but don't scale height.
TEST_F(Ge2dDeviceTest, Scale) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth;
  resize_info_.crop.height = kHeight / 2;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 1,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Scale width down to 25%, but don't scale height.
TEST_F(Ge2dDeviceTest, ScaleQuarter) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth;
  resize_info_.crop.height = kHeight / 4;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Scale height down to 25%, but don't scale width.
TEST_F(Ge2dDeviceTest, ScaleHeightQuarter) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth / 4;
  resize_info_.crop.height = kHeight;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Scale width down to 33%, but don't scale height.
TEST_F(Ge2dDeviceTest, ScaleThird) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth / 4 * 3;
  resize_info_.crop.height = kHeight / 4;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Scale width and height to 2x.
TEST_F(Ge2dDeviceTest, Scale2x) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[1]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth / 2;
  resize_info_.crop.height = kHeight / 2;

  SetFrameCallback([this](const frame_available_info* info) {
    CompareCroppedOutput(info);
    sync_completion_signal(&completion_);
  });

  input_format_index_ = 1;
  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[1], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

TEST_F(Ge2dDeviceTest, NV12ToRgba) {
  SetupCallbacks();
  SetupInput(fuchsia_sysmem_PixelFormatType_NV12, fuchsia_sysmem_PixelFormatType_R8G8B8A8);

  image_format_2_t input_image_format;
  ASSERT_OK(camera::GetImageFormat(input_image_format, fuchsia_sysmem_PixelFormatType_NV12, kWidth,
                                   kHeight));

  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240, input_image_format);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth;
  resize_info_.crop.height = kHeight;

  SetFrameCallback([this](const frame_available_info* info) {
    fprintf(stderr, "Got completed conversion\n");
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));
    uint8_t* output = static_cast<uint8_t*>(mapper_b.start());
    // R
    EXPECT_EQ(0xff, output[0]);
    // G (minor rounding issues)
    EXPECT_EQ(0x01, output[1]);
    // B
    EXPECT_EQ(0x00, output[2]);
    // A
    EXPECT_EQ(0xff, output[3]);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_, &input_image_format,
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Test using SetCropRect.
TEST_F(Ge2dDeviceTest, ChangeScale) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[0]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = kHeight / 4;
  resize_info_.crop.width = kWidth / 2;
  resize_info_.crop.height = kHeight / 4;

  rect_t new_crop_rect = {.x = 0, .y = 0, .width = kWidth, .height = kHeight};
  uint32_t frame_count = 0;
  SetFrameCallback([this, new_crop_rect, &frame_count](const frame_available_info* info) {
    CompareCroppedOutput(info);
    resize_info_.crop = new_crop_rect;
    if (++frame_count == 2)
      sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  ge2d_device_->Ge2dSetCropRect(resize_task, &new_crop_rect);
  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

TEST_F(Ge2dDeviceTest, RemoveTask) {
  SetupCallbacks();
  SetupInput();

  WriteScalingDataToVmo(input_buffer_collection_.buffers[0].vmo, output_image_format_table_[1]);

  resize_info_.crop.x = 0;
  resize_info_.crop.y = 0;
  resize_info_.crop.width = kWidth / 2;
  resize_info_.crop.height = kHeight / 2;

  bool got_frame_callback = false;
  SetFrameCallback([this, &got_frame_callback](const frame_available_info* info) {
    CompareCroppedOutput(info);
    got_frame_callback = true;
  });

  SetTaskRemovedCallback([this](const task_remove_status_t status) {
    EXPECT_EQ(TASK_REMOVE_STATUS_OK, status);
    sync_completion_signal(&completion_);
  });

  input_format_index_ = 1;
  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[1], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  ge2d_device_->Ge2dRemoveTask(resize_task);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
  EXPECT_TRUE(got_frame_callback);
  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_NOT_OK(status);
}

// Test that a resize from an RGBA image to an RGBA image keeps everything
// except for setting alpha to 0xff.
TEST_F(Ge2dDeviceTest, RgbaToRgba) {
  SetupCallbacks();
  SetupInput(fuchsia_sysmem_PixelFormatType_R8G8B8A8, fuchsia_sysmem_PixelFormatType_R8G8B8A8);

  WriteConstantRgbaToVmo(input_buffer_collection_.buffers[0].vmo, 1, 2, 3, 4,
                         output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    fprintf(stderr, "Got completed conversion\n");
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));
    uint8_t* output = static_cast<uint8_t*>(mapper_b.start());
    // R
    EXPECT_EQ(1, output[0]);
    // G
    EXPECT_EQ(2, output[1]);
    // B
    EXPECT_EQ(3, output[2]);
    // A is forced to be 0xff.
    EXPECT_EQ(0xff, output[3]);
    sync_completion_signal(&completion_);
  });

  uint32_t resize_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, &resize_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(resize_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

static void DuplicateWatermarkInfo(const water_mark_info_t& input, const zx::vmo& vmo,
                                   uint32_t count, std::vector<water_mark_info_t>* output) {
  for (uint32_t i = 0; i < count; i++) {
    output->push_back(input);
    output->back().watermark_vmo = vmo.get();
  }
}

// Test that a watermark with 0 alpha doesn't change the input (when copying RGBA to RGBA)
TEST_F(Ge2dDeviceTest, RGBABlankWatermark) {
  SetupCallbacks();
  SetupInput(fuchsia_sysmem_PixelFormatType_R8G8B8A8, fuchsia_sysmem_PixelFormatType_R8G8B8A8);
  SetupWatermarkInfo();

  WriteConstantRgbaToVmo(input_buffer_collection_.buffers[0].vmo, 0x00, 0xff, 0x00, 0xff,
                         output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));
    uint8_t* output = GetPointerToPixel(mapper_b.start(), output_image_format_table_[0],
                                        kWatermarkHorizontalOffset, kWatermarkVerticalOffset);
    // Should match the background color from above.
    // R
    EXPECT_EQ(0x0, output[0]);
    // G
    EXPECT_EQ(0xff, output[1]);
    // B
    EXPECT_EQ(0x00, output[2]);
    // A
    EXPECT_EQ(0xff, output[3]);
    sync_completion_signal(&completion_);
  });

  // Set red to > alpha, to ensure we don't add it in.
  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0xff, 0, 0, 0x00, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Test that a semi-transparent changes colors in the expected way (when copying RGBA to RGBA).
TEST_F(Ge2dDeviceTest, RGBARedWatermark) {
  SetupCallbacks();
  SetupInput(fuchsia_sysmem_PixelFormatType_R8G8B8A8, fuchsia_sysmem_PixelFormatType_R8G8B8A8);
  SetupWatermarkInfo();

  WriteConstantRgbaToVmo(input_buffer_collection_.buffers[0].vmo, 0, 0xff, 0, 0,
                         output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));
    uint8_t* output = GetPointerToPixel(mapper_b.start(), output_image_format_table_[0],
                                        kWatermarkHorizontalOffset, kWatermarkVerticalOffset);
    // Should be an alpha-blended version of both images.
    // R
    EXPECT_EQ(0x7f, output[0]);
    // G
    EXPECT_EQ(0x80, output[1]);
    // B
    EXPECT_EQ(0x00, output[2]);
    // A
    EXPECT_EQ(0xff, output[3]);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0xff, 0, 0, 0x7f, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Simple test that a watermark the same color as the input keeps the output color the same.
TEST_F(Ge2dDeviceTest, SameColorWatermark) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();

  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240,
                          output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    // Red in input and output may be slightly different.
    CompareCroppedOutput(info, 1.0f);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0xff, 0, 0, 0xff, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

TEST_F(Ge2dDeviceTest, NewColorWatermark) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();

  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240,
                          output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_a = input_buffer_collection_.buffers[0].vmo;
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
    image_format_2_t& format = output_image_format_table_[0];

    CacheInvalidateVmo(vmo_a);
    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_a;
    ASSERT_OK(mapper_a.Map(*zx::unowned_vmo(vmo_a), 0, 0, ZX_VM_PERM_READ));
    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));

    // Just check the area above the watermark to make sure it's the same.
    CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), 0, 0, format.bytes_per_row,
                       format.bytes_per_row, format.coded_width, 1, kWatermarkVerticalOffset, 1.0f,
                       1.0f, 0, "Y");
    uint32_t uv_offset = format.bytes_per_row * format.coded_height;
    CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), uv_offset, uv_offset,
                       format.bytes_per_row, format.bytes_per_row, format.coded_width, 2,
                       kWatermarkVerticalOffset / 2, 1.0f, 1.0f, 0, "UV");

    CheckYUVRegion(mapper_b.start(), format, 144, 54, 34, kWatermarkHorizontalOffset,
                   kWatermarkWidth, kWatermarkVerticalOffset, kWatermarkHeight);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0, 0xff, 0, 0xff, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Test that a watermark with 0 alpha doesn't change the input (when copying YUV to YUV)
TEST_F(Ge2dDeviceTest, YUVBlankWatermark) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();

  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240,
                          output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
    image_format_2_t& format = output_image_format_table_[0];

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));

    CheckYUVRegion(mapper_b.start(), format, 82, 90, 240, kWatermarkHorizontalOffset,
                   kWatermarkWidth, kWatermarkVerticalOffset, kWatermarkHeight);

    sync_completion_signal(&completion_);
  });

  // Set red to > alpha, to ensure we don't add it in.
  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0xff, 0, 0, 0x00, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Try switching to size 1 and ensuring the watermark is used correctly.
TEST_F(Ge2dDeviceTest, WatermarkOutputSize) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();
  uint32_t resolution_changed_count = 0;
  SetResolutionChangedCallback([&resolution_changed_count](const frame_available_info* info) {
    ++resolution_changed_count;
  });
  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240,
                          output_image_format_table_[1]);

  static constexpr uint32_t kSecondWatermarkOffset = 4;
  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_a = input_buffer_collection_.buffers[0].vmo;
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
    image_format_2_t& format = output_image_format_table_[1];

    CacheInvalidateVmo(vmo_a);
    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_a;
    ASSERT_OK(mapper_a.Map(*zx::unowned_vmo(vmo_a), 0, 0, ZX_VM_PERM_READ));
    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));

    // Just check the area above the watermark to make sure it's the same.
    CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), 0, 0, format.bytes_per_row,
                       format.bytes_per_row, format.coded_width, 1, kSecondWatermarkOffset, 1.0f,
                       1.0f, 0, "Y");
    uint32_t uv_offset = format.bytes_per_row * format.coded_height;
    CheckSubPlaneEqual(mapper_a.start(), mapper_b.start(), uv_offset, uv_offset,
                       format.bytes_per_row, format.bytes_per_row, format.coded_width, 2,
                       kSecondWatermarkOffset / 2, 1.0f, 1.0f, 0, "UV");

    CheckYUVRegion(mapper_b.start(), format, 144, 54, 34, kSecondWatermarkOffset, kWatermarkWidth,
                   kSecondWatermarkOffset, kWatermarkHeight);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0, 0xff, 0, 0xff, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  duplicated[1].loc_x = kSecondWatermarkOffset;
  duplicated[1].loc_y = kSecondWatermarkOffset;
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dSetInputAndOutputResolution(watermark_task, 1);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
  EXPECT_EQ(1u, resolution_changed_count);
}

TEST_F(Ge2dDeviceTest, GlobalAlphaWatermark) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();

  watermark_info_.global_alpha = 0.5f;

  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[0].vmo, 82, 90, 240,
                          output_image_format_table_[0]);

  SetFrameCallback([this](const frame_available_info* info) {
    zx_handle_t vmo_b = output_buffer_collection_.buffers[info->buffer_id].vmo;
    image_format_2_t& format = output_image_format_table_[0];

    CacheInvalidateVmo(vmo_b);

    fzl::VmoMapper mapper_b;
    ASSERT_OK(mapper_b.Map(*zx::unowned_vmo(vmo_b), 0, 0, ZX_VM_PERM_READ));

    // Blend of 50% red and 50% green.
    CheckYUVRegion(mapper_b.start(), format, 113, 72, 137, kWatermarkHorizontalOffset,
                   kWatermarkWidth, kWatermarkVerticalOffset, kWatermarkHeight);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0, 0xff, 0, 0xff, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated.data(), duplicated.size(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 0);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
}

// Try switching the in-place watermark to size 1 and ensuring the watermark is used correctly.
TEST_F(Ge2dDeviceTest, InPlaceWatermarkOutputSize) {
  SetupCallbacks();
  SetupInput();
  SetupWatermarkInfo();
  uint32_t resolution_changed_count = 0;
  SetResolutionChangedCallback([&resolution_changed_count](const frame_available_info* info) {
    ++resolution_changed_count;
  });
  // Pure red in YUV.
  WriteConstantColorToVmo(input_buffer_collection_.buffers[1].vmo, 82, 90, 240,
                          output_image_format_table_[1]);

  static constexpr uint32_t kSecondWatermarkOffset = 4;
  SetFrameCallback([this](const frame_available_info* info) {
    EXPECT_EQ(1u, info->buffer_id);
    zx_handle_t vmo_a = input_buffer_collection_.buffers[info->buffer_id].vmo;
    image_format_2_t& format = output_image_format_table_[1];

    CacheInvalidateVmo(vmo_a);

    fzl::VmoMapper mapper_a;
    ASSERT_OK(mapper_a.Map(*zx::unowned_vmo(vmo_a), 0, 0, ZX_VM_PERM_READ));

    // Check region above the watermark to ensure it hasn't changed.
    CheckYUVRegion(mapper_a.start(), format, 82, 90, 240, 0, format.coded_width, 0,
                   kSecondWatermarkOffset);

    CheckYUVRegion(mapper_a.start(), format, 144, 54, 34, kSecondWatermarkOffset, kWatermarkWidth,
                   kSecondWatermarkOffset, kWatermarkHeight);
    sync_completion_signal(&completion_);
  });

  zx::vmo watermark_vmo = CreateWatermarkVmo();
  WriteConstantRgbaToVmo(watermark_vmo.get(), 0, 0xff, 0, 0xff, watermark_info_.wm_image_format);
  std::vector<water_mark_info_t> duplicated;
  DuplicateWatermarkInfo(watermark_info_, watermark_vmo, kImageFormatTableSize, &duplicated);
  duplicated[1].loc_x = kSecondWatermarkOffset;
  duplicated[1].loc_y = kSecondWatermarkOffset;
  uint32_t watermark_task;
  zx_status_t status = ge2d_device_->Ge2dInitTaskInPlaceWaterMark(
      &input_buffer_collection_, duplicated.data(), duplicated.size(), output_image_format_table_,
      kImageFormatTableSize, 0, &frame_callback_, &res_callback_, &remove_task_callback_,
      &watermark_task);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dSetInputAndOutputResolution(watermark_task, 1);
  EXPECT_OK(status);

  status = ge2d_device_->Ge2dProcessFrame(watermark_task, 1);
  EXPECT_OK(status);

  EXPECT_EQ(ZX_OK, sync_completion_wait(&completion_, ZX_TIME_INFINITE));
  EXPECT_EQ(1u, resolution_changed_count);
}

}  // namespace

}  // namespace ge2d
