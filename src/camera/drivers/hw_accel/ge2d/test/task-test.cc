// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../task/task.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <stdint.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "../ge2d.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace ge2d {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumberOfBuffers = 8;
constexpr uint32_t kNumberOfMmios = 50;
constexpr uint32_t kImageFormatTableSize = 8;
constexpr uint32_t kWatermarkSize = 1000;
constexpr uint32_t kMaxTasks = 10;

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class TaskTest : public zxtest::Test {
 public:
  void ProcessFrameCallback(uint32_t output_buffer_id) {
    fbl::AutoLock al(&lock_);
    callback_check_.push_back(output_buffer_id);
    frame_ready_ = true;
    event_.Signal();
  }

  void WaitAndReset() {
    fbl::AutoLock al(&lock_);
    while (frame_ready_ == false) {
      event_.Wait(&lock_);
    }
    frame_ready_ = false;
  }

  uint32_t GetCallbackSize() {
    fbl::AutoLock al(&lock_);
    return callback_check_.size();
  }

  uint32_t GetCallbackBack() {
    fbl::AutoLock al(&lock_);
    return callback_check_.back();
  }

 protected:
  void SetUpBufferCollections(uint32_t buffer_collection_count) {
    frame_ready_ = false;
    camera::GetImageFormat2(input_image_format_, kWidth, kHeight);
    camera::GetImageFormat2(output_image_format_table_[0], kWidth, kHeight);
    camera::GetImageFormat2(output_image_format_table_[1], kWidth / 2, kHeight / 2);
    camera::GetImageFormat2(output_image_format_table_[2], kWidth / 4, kHeight / 4);
    // Set up fake Resize info, Watermark info, Watermark vmo.
    resize_info_.crop.crop_x = 100;
    resize_info_.crop.crop_y = 100;
    resize_info_.crop.crop_width = 50;
    resize_info_.crop.crop_height = 50;
    resize_info_.output_rotation = GE2D_ROTATION_ROTATION_0;
    watermark_info_.loc_x = 100;
    watermark_info_.loc_y = 100;
    camera::GetImageFormat2(watermark_info_.wm_image_format, kWidth / 4, kHeight / 4);
    ASSERT_OK(fake_bti_create(bti_handle_.reset_and_get_address()));
    zx_status_t status = zx_vmo_create_contiguous(bti_handle_.get(), kWatermarkSize, 0,
                                                  watermark_vmo_.reset_and_get_address());
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo2(
        input_buffer_collection_, input_image_format_, bti_handle_.get(), kWidth, kHeight,
        buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo2(
        output_buffer_collection_, input_image_format_, bti_handle_.get(), kWidth, kHeight,
        buffer_collection_count);
    ASSERT_OK(status);
  }

  // Sets up Ge2dDevice, initialize a task.
  // Returns a task id.
  zx_status_t SetupForFrameProcessing(ddk_mock::MockMmioRegRegion& fake_regs, uint32_t& resize_task,
                                      uint32_t& watermark_task) {
    SetUpBufferCollections(kNumberOfBuffers);

    zx::port port;
    EXPECT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

    callback_.frame_ready = [](void* ctx, const frame_available_info* info) {
      return static_cast<TaskTest*>(ctx)->ProcessFrameCallback(info->buffer_id);
    };
    callback_.ctx = this;

    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    EXPECT_OK(port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_));

    zx::interrupt irq;
    EXPECT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq));

    ge2d_device_ =
        std::make_unique<Ge2dDevice>(nullptr, ddk::MmioBuffer(fake_regs.GetMmioBuffer()),
                                     std::move(irq), std::move(bti_handle_), std::move(port));

    uint32_t task_id;
    zx::vmo watermark_vmo;
    EXPECT_OK(watermark_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &watermark_vmo));
    zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
        &input_buffer_collection_, &output_buffer_collection_, &watermark_info_,
        std::move(watermark_vmo), &input_image_format_, output_image_format_table_,
        kImageFormatTableSize, 0, &callback_, &task_id);
    EXPECT_OK(status);
    watermark_task = task_id;

    status = ge2d_device_->Ge2dInitTaskResize(
        &input_buffer_collection_, &output_buffer_collection_, &resize_info_, &input_image_format_,
        output_image_format_table_, kImageFormatTableSize, 0, &callback_, &task_id);
    EXPECT_OK(status);
    resize_task = task_id;

    // Start the thread.
    EXPECT_OK(ge2d_device_->StartThread());

    return ZX_OK;
  }

  void TearDown() override {
    if (bti_handle_ != ZX_HANDLE_INVALID) {
      fake_bti_destroy(bti_handle_.get());
    }
  }

  zx::vmo watermark_vmo_;
  zx::bti bti_handle_;
  zx::port port_;
  zx::interrupt irq_;
  hw_accel_callback_t callback_;
  // Array of output Image formats.
  image_format_2_t output_image_format_table_[kImageFormatTableSize];
  resize_info_t resize_info_;
  water_mark_info_t watermark_info_;
  buffer_collection_info_2_t input_buffer_collection_;
  buffer_collection_info_2_t output_buffer_collection_;
  // Input Image format.
  image_format_2_t input_image_format_;
  std::unique_ptr<Ge2dDevice> ge2d_device_;
private:
  std::vector<uint32_t> callback_check_;
  bool frame_ready_;
  fbl::Mutex lock_;
  fbl::ConditionVariable event_;
};

TEST_F(TaskTest, BasicCreationTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  fbl::AllocChecker ac;
  auto resize_task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  auto watermark_task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = resize_task->InitResize(&input_buffer_collection_, &output_buffer_collection_,
                                   &resize_info_, &input_image_format_, output_image_format_table_,
                                   kImageFormatTableSize, 0, &callback_, bti_handle_);
  EXPECT_OK(status);
  status = watermark_task->InitWatermark(&input_buffer_collection_, &output_buffer_collection_,
                                         &watermark_info_, watermark_vmo_, &input_image_format_,
                                         output_image_format_table_, kImageFormatTableSize, 0,
                                         &callback_, bti_handle_);
  EXPECT_OK(status);
}

TEST_F(TaskTest, WatermarkResTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  fbl::AllocChecker ac;
  auto wm_task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = wm_task->InitWatermark(&input_buffer_collection_, &output_buffer_collection_,
                                  &watermark_info_, watermark_vmo_,
                                  &input_image_format_, output_image_format_table_,
                                  kImageFormatTableSize, 0, &callback_, bti_handle_);
  EXPECT_OK(status);
  image_format_2_t format = wm_task->WatermarkFormat();
  EXPECT_EQ(format.display_width, kWidth / 4);
  EXPECT_EQ(format.display_height, kHeight / 4);
}

TEST_F(TaskTest, OutputResTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  fbl::AllocChecker ac;
  auto resize_task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = resize_task->InitResize(&input_buffer_collection_, &output_buffer_collection_,
                                   &resize_info_, &input_image_format_, output_image_format_table_,
                                   kImageFormatTableSize, 2, &callback_, bti_handle_);
  EXPECT_OK(status);
  image_format_2_t format = resize_task->output_format();
  EXPECT_EQ(format.display_width, kWidth / 4);
  EXPECT_EQ(format.display_height, kHeight / 4);
}

TEST_F(TaskTest, InvalidFormatTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  image_format_2_t format;
  camera::GetImageFormat2(format, kWidth, kHeight);
  format.pixel_format.type = fuchsia_sysmem_PixelFormatType_YUY2;
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            task->InitResize(&input_buffer_collection_, &output_buffer_collection_, &resize_info_,
                             &format, output_image_format_table_, kImageFormatTableSize, 0,
                             &callback_, bti_handle_));
}

TEST_F(TaskTest, InvalidVmoTest) {
  SetUpBufferCollections(0);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  zx_status_t status;
  status = task->InitResize(&input_buffer_collection_, &output_buffer_collection_, &resize_info_,
                            &input_image_format_, output_image_format_table_, kImageFormatTableSize,
                            0, &callback_, bti_handle_);
  // Expecting Task setup to be returning an error when there are
  // no VMOs in the buffer collection. At the moment VmoPool library
  // doesn't return an error.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST_F(TaskTest, InitTaskTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED uint32_t resize_task_id, watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  std::vector<uint32_t> received_ids;
  for (uint32_t i = 0; i < kMaxTasks; i++) {
    uint32_t task_id;
    zx_status_t status = ge2d_device_->Ge2dInitTaskResize(
        &input_buffer_collection_, &output_buffer_collection_, &resize_info_, &input_image_format_,
        output_image_format_table_, kImageFormatTableSize, 0, &callback_, &task_id);
    EXPECT_OK(status);
    // Check to see if we are getting unique task ids.
    auto entry = find(received_ids.begin(), received_ids.end(), task_id);
    EXPECT_EQ(received_ids.end(), entry);
    received_ids.push_back(task_id);
    zx::vmo watermark_vmo;
    ASSERT_OK(watermark_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &watermark_vmo));
    status = ge2d_device_->Ge2dInitTaskWaterMark(
        &input_buffer_collection_, &output_buffer_collection_, &watermark_info_,
        std::move(watermark_vmo), &input_image_format_, output_image_format_table_,
        kImageFormatTableSize, 0, &callback_, &task_id);
    EXPECT_OK(status);
    // Check to see if we are getting unique task ids.
    entry = find(received_ids.begin(), received_ids.end(), task_id);
    EXPECT_EQ(received_ids.end(), entry);
    received_ids.push_back(task_id);
  }
  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, RemoveTaskTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id, watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Valid id.
  ASSERT_NO_DEATH(([this, resize_task_id]() { ge2d_device_->Ge2dRemoveTask(resize_task_id); }));
  ASSERT_NO_DEATH(
      ([this, watermark_task_id]() { ge2d_device_->Ge2dRemoveTask(watermark_task_id); }));

  // Invalid id.
  ASSERT_DEATH(([this, resize_task_id]() { ge2d_device_->Ge2dRemoveTask(resize_task_id + 10); }));
  ASSERT_DEATH(
      ([this, watermark_task_id]() { ge2d_device_->Ge2dRemoveTask(watermark_task_id + 10); }));

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, ProcessInvalidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED uint32_t resize_task_id, watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Invalid task id.
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(0xFF, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, InvalidBufferProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  __UNUSED uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Invalid buffer id.
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(watermark_task_id, kNumberOfBuffers);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, ProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Valid buffer & task id.
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  ASSERT_OK(ge2d_device_->StopThread());

  fake_regs.VerifyAll();
}

TEST_F(TaskTest, ReleaseValidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Valid buffer & task id.
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  // There is no output buffer to release at the moment. But let's keep this code
  // in place so we can add a test for this later.
  ASSERT_NO_DEATH(([this, resize_task_id]() {
    ge2d_device_->Ge2dReleaseFrame(resize_task_id, GetCallbackBack());
  }));

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, ReleaseInValidFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Valid buffer & task id.
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());

  // Test releasing an invalid frame (invalid task id).
  ASSERT_DEATH(([this, resize_task_id]() {
    ge2d_device_->Ge2dReleaseFrame(resize_task_id + 1, GetCallbackBack());
  }));

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, MultipleProcessFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // Process few frames, putting them in a queue
  zx_status_t status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);
  status = ge2d_device_->Ge2dProcessFrame(watermark_task_id, kNumberOfBuffers - 2);
  EXPECT_OK(status);
  status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 3);
  EXPECT_OK(status);
  status = ge2d_device_->Ge2dProcessFrame(watermark_task_id, kNumberOfBuffers - 4);
  EXPECT_OK(status);

  // Trigger interrupt manually thrice, making sure callback is called once
  // each time.
  for (uint32_t t = 1; t <= 3; t++) {
    // Trigger the interrupt manually.
    zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
    EXPECT_OK(port_.queue(&packet));

    // Check if the callback was called once.
    WaitAndReset();
    EXPECT_EQ(t, GetCallbackSize());
  }

  // This time adding another frame to process while its
  // waiting for an interrupt.
  status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 5);
  EXPECT_OK(status);

  for (uint32_t t = 1; t <= 2; t++) {
    // Trigger the interrupt manually.
    zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
    EXPECT_OK(port_.queue(&packet));

    // Check if the callback was called once.
    WaitAndReset();
    EXPECT_EQ(t + 3, GetCallbackSize());
  }

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST(TaskTest, NonContigVmoTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  hw_accel_callback_t callback;
  zx_handle_t watermark_vmo;
  buffer_collection_info_2_t input_buffer_collection;
  buffer_collection_info_2_t output_buffer_collection;
  image_format_2_t image_format_table[kImageFormatTableSize];
  water_mark_info_t watermark_info;
  ASSERT_OK(fake_bti_create(&bti_handle));

  image_format_2_t format;
  camera::GetImageFormat2(format, kWidth, kHeight);
  camera::GetImageFormat2(image_format_table[0], kWidth, kHeight);
  zx_status_t status = camera::CreateContiguousBufferCollectionInfo2(
      input_buffer_collection, format, bti_handle, kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = camera::CreateContiguousBufferCollectionInfo2(output_buffer_collection, format,
                                                         bti_handle, kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = zx_vmo_create(kWatermarkSize, 0, &watermark_vmo);
  ASSERT_OK(status);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  camera::GetImageFormat2(watermark_info.wm_image_format, kWidth / 4, kHeight / 4);
  status = task->InitWatermark(&input_buffer_collection, &output_buffer_collection, &watermark_info,
                               zx::vmo(watermark_vmo), &format, image_format_table,
                               kImageFormatTableSize, 0, &callback, zx::bti(bti_handle));
  // Expecting Task setup to be returning an error when watermark vmo is not
  // contig.
  EXPECT_NE(ZX_OK, status);
}

TEST(TaskTest, InvalidBufferCollectionTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  hw_accel_callback_t callback;
  zx_handle_t watermark_vmo;
  image_format_2_t image_format_table[kImageFormatTableSize];
  water_mark_info_t watermark_info;
  ASSERT_OK(fake_bti_create(&bti_handle));

  zx_status_t status = zx_vmo_create_contiguous(bti_handle, kWatermarkSize, 0, &watermark_vmo);
  ASSERT_OK(status);
  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Ge2dTask>(new (&ac) Ge2dTask());
  EXPECT_TRUE(ac.check());
  image_format_2_t format;
  camera::GetImageFormat2(format, kWidth, kHeight);
  camera::GetImageFormat2(image_format_table[0], kWidth, kHeight);
  camera::GetImageFormat2(watermark_info.wm_image_format, kWidth / 4, kHeight / 4);
  status = task->InitWatermark(nullptr, nullptr, &watermark_info, zx::vmo(watermark_vmo), &format,
                               image_format_table, kImageFormatTableSize, 0, &callback,
                               zx::bti(bti_handle));
  EXPECT_NE(ZX_OK, status);
}

}  // namespace
}  // namespace ge2d
