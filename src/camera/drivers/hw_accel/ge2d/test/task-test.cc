// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/hw_accel/task/task.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/image-format/image_format.h>
#include <lib/mmio/mmio.h>
#include <lib/syslog/global.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "src/camera/drivers/hw_accel/ge2d/ge2d.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace ge2d {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 768;
constexpr uint32_t kNumberOfBuffers = 8;
constexpr uint32_t kNumberOfMmios = 1000;
constexpr uint32_t kImageFormatTableSize = 8;
constexpr uint32_t kMaxTasks = 10;

static uint8_t GenFakeCanvasId(zx_handle_t vmo) {
  zx_info_handle_basic_t info;
  zx_object_get_info(vmo, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);

  uint8_t out_id = 0;
  for (uint32_t i = 0; i < 8; i++) {
    out_id ^= (info.koid >> (i * 8)) & 0xff;
  }

  return out_id;
}

zx_status_t FakeCanvasConfig(void* ctx, zx_handle_t vmo, size_t offset, const canvas_info_t* info,
                             uint8_t* out_canvas_idx) {
  *out_canvas_idx = GenFakeCanvasId(vmo);
  zx_handle_close(vmo);
  return ZX_OK;
}

zx_status_t FakeCanvasFree(void* ctx, uint8_t canvas_idx) { return ZX_OK; }

amlogic_canvas_protocol_ops_t fake_canvas_ops = {FakeCanvasConfig, FakeCanvasFree};
amlogic_canvas_protocol_t fake_canvas = {&fake_canvas_ops, NULL};

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

void DuplicateWatermarkInfo(const water_mark_info_t& input, const zx::vmo& vmo, uint32_t count,
                            std::vector<water_mark_info_t>* output) {
  for (uint32_t i = 0; i < count; i++) {
    output->push_back(input);
    zx::vmo dup_vmo;
    vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
    output->back().watermark_vmo = dup_vmo.release();
  }
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class TaskTest : public zxtest::Test {
 public:
  void ProcessFrameCallback(uint32_t input_buffer_index, uint32_t output_buffer_index,
                            frame_status_t status) {
    fbl::AutoLock al(&lock_);
    callback_check_.emplace_back(input_buffer_index, output_buffer_index);
    frame_ready_ = true;
    event_.Signal();
    if (status != FRAME_STATUS_OK) {
      frame_status_error_ = true;
    }
  }

  void ResChangeCallback() {
    fbl::AutoLock al(&lock_);
    frame_ready_ = true;
    event_.Signal();
  }

  void RemoveTaskCallback(task_remove_status_t status) {
    fbl::AutoLock al(&lock_);
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

  uint32_t GetCallbackBackOutputBufferIndex() {
    fbl::AutoLock al(&lock_);
    return callback_check_.back().second;
  }

  uint32_t GetCallbackBackInputBufferIndex() {
    fbl::AutoLock al(&lock_);
    return callback_check_.back().first;
  }

 protected:
  enum class TaskType { kResize, kWatermark };

  void SetUpBufferCollections(uint32_t buffer_collection_count) {
    frame_ready_ = false;
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[0],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[1],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth / 2, kHeight / 2));
    ASSERT_OK(camera::GetImageFormat(output_image_format_table_[2],
                                     fuchsia_sysmem_PixelFormatType_NV12, kWidth / 4, kHeight / 4));
    // Set up fake Resize info, Watermark info, Watermark vmo.
    resize_info_.crop.x = 100;
    resize_info_.crop.y = 100;
    resize_info_.crop.width = 50;
    resize_info_.crop.height = 50;
    resize_info_.output_rotation = GE2D_ROTATION_ROTATION_0;
    watermark_info_.loc_x = 100;
    watermark_info_.loc_y = 100;
    ASSERT_OK(camera::GetImageFormat(watermark_info_.wm_image_format,
                                     fuchsia_sysmem_PixelFormatType_R8G8B8A8, kWidth / 4,
                                     kHeight / 4));
    ASSERT_OK(fake_bti_create(bti_handle_.reset_and_get_address()));
    uint32_t watermark_size = ImageFormatImageSize(&watermark_info_.wm_image_format);

    zx_status_t status = zx_vmo_create_contiguous(bti_handle_.get(), watermark_size, 0,
                                                  watermark_vmo_.reset_and_get_address());
    ASSERT_OK(status);

    DuplicateWatermarkInfo(watermark_info_, watermark_vmo_, kImageFormatTableSize,
                           &duplicated_watermark_info_);

    status = camera::CreateContiguousBufferCollectionInfo(
        input_buffer_collection_, output_image_format_table_[0], bti_handle_.get(),
        buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo(
        output_buffer_collection_, output_image_format_table_[0], bti_handle_.get(),
        buffer_collection_count);
    ASSERT_OK(status);
  }

  // This should be called after a watermark task has taken ownership of the watermark info.
  void ClearWatermarkVmos() {
    for (auto& watermark : duplicated_watermark_info_) {
      watermark.watermark_vmo = ZX_HANDLE_INVALID;
    }
  }

  // Sets up Ge2dDevice, initialize a task.
  // Returns a task id.
  zx_status_t SetupForFrameProcessing(ddk_mock::MockMmioRegRegion& fake_regs, uint32_t& resize_task,
                                      uint32_t& watermark_task) {
    SetUpBufferCollections(kNumberOfBuffers);

    zx::port port;
    EXPECT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

    frame_callback_.frame_ready = [](void* ctx, const frame_available_info* info) {
      EXPECT_EQ(static_cast<TaskTest*>(ctx)->output_image_format_index_,
                info->metadata.image_format_index);
      return static_cast<TaskTest*>(ctx)->ProcessFrameCallback(info->metadata.input_buffer_index,
                                                               info->buffer_id, info->frame_status);
    };
    frame_callback_.ctx = this;

    res_callback_.frame_resolution_changed = [](void* ctx, const frame_available_info* info) {
      EXPECT_EQ(static_cast<TaskTest*>(ctx)->output_image_format_index_,
                info->metadata.image_format_index);
      return static_cast<TaskTest*>(ctx)->ResChangeCallback();
    };
    res_callback_.ctx = this;

    remove_task_callback_.task_removed = [](void* ctx, task_remove_status_t status) {
      return static_cast<TaskTest*>(ctx)->RemoveTaskCallback(status);
    };
    remove_task_callback_.ctx = this;

    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    EXPECT_OK(port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_));

    zx::interrupt irq;
    EXPECT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq));

    ge2d_device_ = std::make_unique<Ge2dDevice>(nullptr, ddk::MmioBuffer(fake_regs.GetMmioBuffer()),
                                                std::move(irq), std::move(bti_handle_),
                                                std::move(port), fake_canvas);

    uint32_t task_id;
    zx::vmo watermark_vmo;
    EXPECT_OK(watermark_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &watermark_vmo));
    zx_status_t status = ge2d_device_->Ge2dInitTaskWaterMark(
        &input_buffer_collection_, &output_buffer_collection_, duplicated_watermark_info_.data(),
        duplicated_watermark_info_.size(), output_image_format_table_, kImageFormatTableSize, 0,
        &frame_callback_, &res_callback_, &remove_task_callback_, &task_id);
    EXPECT_OK(status);
    watermark_task = task_id;

    status = ge2d_device_->Ge2dInitTaskResize(
        &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
        &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
        &frame_callback_, &res_callback_, &remove_task_callback_, &task_id);
    EXPECT_OK(status);
    resize_task = task_id;
    output_image_format_index_ = 0;

    // Start the thread.
    EXPECT_OK(ge2d_device_->StartThread());

    return ZX_OK;
  }

  void TearDown() override {
    EXPECT_OK(camera::DestroyContiguousBufferCollection(input_buffer_collection_));
    EXPECT_OK(camera::DestroyContiguousBufferCollection(output_buffer_collection_));
    for (auto& watermark : duplicated_watermark_info_) {
      zx_handle_close(watermark.watermark_vmo);
    }
  }

  void TriggerInterrupts(TaskType type) {
    uint32_t count = (type == TaskType::kWatermark) ? 3 : 1;
    for (uint32_t i = 0; i < count; i++) {
      // Trigger the interrupt manually.
      zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
      EXPECT_OK(port_.queue(&packet));
    }
  }

  zx::vmo watermark_vmo_;
  zx::bti bti_handle_;
  zx::port port_;
  zx::interrupt irq_;
  hw_accel_frame_callback_t frame_callback_;
  hw_accel_res_change_callback_t res_callback_;
  hw_accel_remove_task_callback_t remove_task_callback_;
  // Array of output Image formats.
  image_format_2_t output_image_format_table_[kImageFormatTableSize];
  resize_info_t resize_info_;
  water_mark_info_t watermark_info_{};
  std::vector<water_mark_info_t> duplicated_watermark_info_;
  buffer_collection_info_2_t input_buffer_collection_;
  buffer_collection_info_2_t output_buffer_collection_;
  std::unique_ptr<Ge2dDevice> ge2d_device_;
  uint32_t output_image_format_index_;
  bool frame_status_error_ = false;

 private:
  std::vector<std::pair<uint32_t, uint32_t>> callback_check_;
  bool frame_ready_;
  fbl::Mutex lock_;
  fbl::ConditionVariable event_;
};

TEST_F(TaskTest, BasicCreationTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  auto resize_task = std::make_unique<Ge2dTask>();
  auto watermark_task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = resize_task->InitResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
      &frame_callback_, &res_callback_, &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
  status = watermark_task->InitWatermark(
      &input_buffer_collection_, &output_buffer_collection_, duplicated_watermark_info_.data(),
      output_image_format_table_, kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
      &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
}

// This test sanity checks the canvas id's returned for a Frame by generating
// a fake canvas id that is a function of the vmo handle.
TEST_F(TaskTest, CanvasIdTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  auto task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = task->InitResize(&input_buffer_collection_, &output_buffer_collection_, &resize_info_,
                            &output_image_format_table_[0], output_image_format_table_,
                            kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
                            &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
  for (uint32_t i = 0; i < input_buffer_collection_.buffer_count; i++) {
    zx_handle_t vmo_handle = input_buffer_collection_.buffers[i].vmo;
    const image_canvas_id_t& canvas_ids = task->GetInputCanvasIds(i);
    // We only test the UV Frame here because for the canvas id allocation of
    // the Y frame, the vmo handle is duplicated, so the vmo's won't match.
    EXPECT_EQ(GenFakeCanvasId(vmo_handle), canvas_ids.canvas_idx[kUVComponent].id());
  }
  std::deque<fzl::VmoPool::Buffer> output_buffers;
  for (uint32_t i = 0; i < output_buffer_collection_.buffer_count; i++) {
    output_buffers.push_front(*task->WriteLockOutputBuffer());
  }
  uint32_t count = 0;
  while (!output_buffers.empty()) {
    count++;
    auto buffer = std::move(output_buffers.back());
    output_buffers.pop_back();
    zx_handle_t vmo_handle = buffer.vmo_handle();
    const image_canvas_id_t& canvas_ids = task->GetOutputCanvasIds(vmo_handle);
    // We only test the UV Frame here because for the canvas id allocation of
    // the Y frame, the vmo handle is duplicated, so the vmo's won't match.
    EXPECT_EQ(GenFakeCanvasId(vmo_handle), canvas_ids.canvas_idx[kUVComponent].id());
    task->ReleaseOutputBuffer(std::move(buffer));
  }
  ZX_ASSERT(count == output_buffer_collection_.buffer_count);
}

TEST_F(TaskTest, WatermarkResTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  auto wm_task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = wm_task->InitWatermark(&input_buffer_collection_, &output_buffer_collection_,
                                  duplicated_watermark_info_.data(), output_image_format_table_,
                                  kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
                                  &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
  image_format_2_t format = wm_task->WatermarkFormat();
  EXPECT_EQ(format.display_width, kWidth / 4);
  EXPECT_EQ(format.display_height, kHeight / 4);
}

TEST_F(TaskTest, InitOutputResTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  auto resize_task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = resize_task->InitResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[2], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
  image_format_2_t format = resize_task->output_format();
  EXPECT_EQ(format.display_width, kWidth / 4);
  EXPECT_EQ(format.display_height, kHeight / 4);
}

TEST_F(TaskTest, InitInputResTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  auto resize_task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = resize_task->InitResize(
      &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
      &output_image_format_table_[2], output_image_format_table_, kImageFormatTableSize, 2,
      &frame_callback_, &res_callback_, &remove_task_callback_, bti_handle_, fake_canvas);
  EXPECT_OK(status);
  image_format_2_t format = resize_task->input_format();
  EXPECT_EQ(format.display_width, kWidth / 4);
  EXPECT_EQ(format.display_height, kHeight / 4);
}

TEST_F(TaskTest, InvalidFormatTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  image_format_2_t format;
  EXPECT_OK(camera::GetImageFormat(format, fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight));
  format.pixel_format.type = ZX_PIXEL_FORMAT_MONO_8;
  auto task = std::make_unique<Ge2dTask>();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            task->InitResize(&input_buffer_collection_, &output_buffer_collection_, &resize_info_,
                             &format, output_image_format_table_, kImageFormatTableSize, 0,
                             &frame_callback_, &res_callback_, &remove_task_callback_, bti_handle_,
                             fake_canvas));
}

TEST_F(TaskTest, InvalidVmoTest) {
  SetUpBufferCollections(0);
  auto task = std::make_unique<Ge2dTask>();
  zx_status_t status;
  status = task->InitResize(&input_buffer_collection_, &output_buffer_collection_, &resize_info_,
                            &output_image_format_table_[0], output_image_format_table_,
                            kImageFormatTableSize, 0, &frame_callback_, &res_callback_,
                            &remove_task_callback_, bti_handle_, fake_canvas);
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
        &input_buffer_collection_, &output_buffer_collection_, &resize_info_,
        &output_image_format_table_[0], output_image_format_table_, kImageFormatTableSize, 0,
        &frame_callback_, &res_callback_, &remove_task_callback_, &task_id);
    EXPECT_OK(status);
    // Check to see if we are getting unique task ids.
    auto entry = find(received_ids.begin(), received_ids.end(), task_id);
    EXPECT_EQ(received_ids.end(), entry);
    received_ids.push_back(task_id);
    std::vector<water_mark_info_t> watermark_info;
    DuplicateWatermarkInfo(watermark_info_, watermark_vmo_, kImageFormatTableSize, &watermark_info);
    status = ge2d_device_->Ge2dInitTaskWaterMark(
        &input_buffer_collection_, &output_buffer_collection_, watermark_info.data(),
        watermark_info.size(), output_image_format_table_, kImageFormatTableSize, 0,
        &frame_callback_, &res_callback_, &remove_task_callback_, &task_id);
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
  EXPECT_FALSE(frame_status_error_);

  ASSERT_OK(ge2d_device_->StopThread());

  fake_regs.VerifyAll();
}

TEST_F(TaskTest, SetOutputResTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  output_image_format_index_ = 2;
  zx_status_t status = ge2d_device_->Ge2dSetOutputResolution(resize_task_id, 2);
  EXPECT_OK(status);
  WaitAndReset();

  // Valid buffer & task id.
  status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  // The callback first tests to make sure the output res index matches what we
  // changed it to above.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());
  EXPECT_FALSE(frame_status_error_);

  ASSERT_OK(ge2d_device_->StopThread());

  fake_regs.VerifyAll();
}

TEST_F(TaskTest, SetInputAndOutputResTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  zx_status_t status = ge2d_device_->Ge2dSetInputAndOutputResolution(resize_task_id, 2);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  output_image_format_index_ = 2;
  status = ge2d_device_->Ge2dSetInputAndOutputResolution(watermark_task_id, 2);
  EXPECT_OK(status);
  WaitAndReset();

  // Valid buffer & task id.
  status = ge2d_device_->Ge2dProcessFrame(watermark_task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the three interrupts manually.
  TriggerInterrupts(TaskType::kWatermark);
  // Check if the callback was called.
  // The callback first tests to make sure the output res index matches what we
  // changed it to above.
  WaitAndReset();
  EXPECT_EQ(1, GetCallbackSize());
  EXPECT_FALSE(frame_status_error_);

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
  EXPECT_FALSE(frame_status_error_);

  // There is no output buffer to release at the moment. But let's keep this code
  // in place so we can add a test for this later.
  ASSERT_NO_DEATH(([this, resize_task_id]() {
    ge2d_device_->Ge2dReleaseFrame(resize_task_id, GetCallbackBackOutputBufferIndex());
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
  EXPECT_FALSE(frame_status_error_);

  // Test releasing an invalid frame (invalid task id).
  ASSERT_DEATH(([this, resize_task_id]() {
    ge2d_device_->Ge2dReleaseFrame(resize_task_id + 1, GetCallbackBackOutputBufferIndex());
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

  constexpr TaskType kType[] = {
      TaskType::kResize,
      TaskType::kWatermark,
      TaskType::kResize,
  };

  // Trigger interrupt manually thrice, making sure callback is called once
  // each time.
  for (uint32_t t = 1; t <= 3; t++) {
    TriggerInterrupts(kType[t - 1]);

    // Check if the callback was called once.
    WaitAndReset();
    EXPECT_EQ(t, GetCallbackSize());
    EXPECT_FALSE(frame_status_error_);
    EXPECT_EQ(kNumberOfBuffers - t, GetCallbackBackInputBufferIndex());
  }

  // This time adding another frame to process while its
  // waiting for an interrupt.
  status = ge2d_device_->Ge2dProcessFrame(resize_task_id, kNumberOfBuffers - 5);
  EXPECT_OK(status);

  constexpr TaskType kType2[] = {
      TaskType::kWatermark,
      TaskType::kResize,
  };
  for (uint32_t t = 1; t <= 2; t++) {
    TriggerInterrupts(kType2[t - 1]);

    // Check if the callback was called once.
    WaitAndReset();
    EXPECT_EQ(t + 3, GetCallbackSize());
    EXPECT_FALSE(frame_status_error_);
    EXPECT_EQ(kNumberOfBuffers - (t + 3), GetCallbackBackInputBufferIndex());
  }

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST_F(TaskTest, DropFrameTest) {
  ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
  ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

  uint32_t resize_task_id;
  uint32_t watermark_task_id;
  ASSERT_OK(SetupForFrameProcessing(fake_regs, resize_task_id, watermark_task_id));

  // We process kNumberOfBuffers frames.
  for (uint32_t i = 0; i < kNumberOfBuffers; i++) {
    auto status = ge2d_device_->Ge2dProcessFrame(resize_task_id, i);
    EXPECT_OK(status);
  }

  // Ensure that all of them are processed. This ensures that all o/p buffers are used.
  for (uint32_t t = 1; t <= kNumberOfBuffers; t++) {
    // Trigger the interrupt manually.
    zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
    EXPECT_OK(port_.queue(&packet));

    // Check if the callback was called.
    WaitAndReset();
    EXPECT_EQ(t, GetCallbackSize());
    EXPECT_FALSE(frame_status_error_);
    EXPECT_EQ(t - 1, GetCallbackBackInputBufferIndex());
  }

  // Adding one more frame to process.
  auto status = ge2d_device_->Ge2dProcessFrame(resize_task_id, 0);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  WaitAndReset();
  EXPECT_EQ(kNumberOfBuffers + 1, GetCallbackSize());
  EXPECT_TRUE(frame_status_error_);

  ASSERT_OK(ge2d_device_->StopThread());
}

TEST(TaskTest, NonContigVmoTest) {
  zx::bti bti_handle;
  hw_accel_frame_callback_t frame_callback;
  hw_accel_res_change_callback_t res_callback;
  hw_accel_remove_task_callback_t remove_task_callback;

  zx::vmo watermark_vmo;
  buffer_collection_info_2_t input_buffer_collection;
  buffer_collection_info_2_t output_buffer_collection;
  image_format_2_t image_format_table[kImageFormatTableSize];
  water_mark_info_t watermark_info;
  ASSERT_OK(fake_bti_create(bti_handle.reset_and_get_address()));

  image_format_2_t format;
  EXPECT_OK(camera::GetImageFormat(format, fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight));
  EXPECT_OK(camera::GetImageFormat(image_format_table[0], fuchsia_sysmem_PixelFormatType_NV12,
                                   kWidth, kHeight));
  zx_status_t status = camera::CreateContiguousBufferCollectionInfo(input_buffer_collection, format,
                                                                    bti_handle.get(), 0);
  ASSERT_OK(status);

  status = camera::CreateContiguousBufferCollectionInfo(output_buffer_collection, format,
                                                        bti_handle.get(), 0);
  ASSERT_OK(status);

  uint32_t watermark_size =
      (kWidth / 4) * (kHeight / 4) * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_ARGB_8888);
  status = zx::vmo::create_contiguous(bti_handle, watermark_size, 0, &watermark_vmo);
  ASSERT_OK(status);
  auto task = std::make_unique<Ge2dTask>();
  EXPECT_OK(camera::GetImageFormat(watermark_info.wm_image_format,
                                   fuchsia_sysmem_PixelFormatType_R8G8B8A8, kWidth / 4,
                                   kHeight / 4));
  std::vector<water_mark_info_t> duplicated_watermark_info;
  DuplicateWatermarkInfo(watermark_info, watermark_vmo, kImageFormatTableSize,
                         &duplicated_watermark_info);
  status = task->InitWatermark(&input_buffer_collection, &output_buffer_collection,
                               duplicated_watermark_info.data(), image_format_table,
                               kImageFormatTableSize, 0, &frame_callback, &res_callback,
                               &remove_task_callback, bti_handle, fake_canvas);
  // Expecting Task setup to be returning an error when watermark vmo is not
  // contig.
  EXPECT_NE(ZX_OK, status);

  // Cleanup
  EXPECT_OK(camera::DestroyContiguousBufferCollection(input_buffer_collection));
  EXPECT_OK(camera::DestroyContiguousBufferCollection(output_buffer_collection));
}

TEST(TaskTest, InvalidBufferCollectionTest) {
  zx::bti bti_handle;
  hw_accel_frame_callback_t frame_callback;
  hw_accel_res_change_callback_t res_callback;
  hw_accel_remove_task_callback_t remove_task_callback;

  zx::vmo watermark_vmo;
  image_format_2_t image_format_table[kImageFormatTableSize];
  water_mark_info_t watermark_info;
  ASSERT_OK(fake_bti_create(bti_handle.reset_and_get_address()));

  uint32_t watermark_size =
      (kWidth / 4) * (kHeight / 4) * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_ARGB_8888);
  zx_status_t status = zx::vmo::create_contiguous(bti_handle, watermark_size, 0, &watermark_vmo);
  ASSERT_OK(status);
  auto task = std::make_unique<Ge2dTask>();
  image_format_2_t format;
  EXPECT_OK(camera::GetImageFormat(format, fuchsia_sysmem_PixelFormatType_NV12, kWidth, kHeight));
  EXPECT_OK(camera::GetImageFormat(image_format_table[0], fuchsia_sysmem_PixelFormatType_NV12,
                                   kWidth, kHeight));
  EXPECT_OK(camera::GetImageFormat(watermark_info.wm_image_format,
                                   fuchsia_sysmem_PixelFormatType_R8G8B8A8, kWidth / 4,
                                   kHeight / 4));
  std::vector<water_mark_info_t> duplicated_watermark_info;
  DuplicateWatermarkInfo(watermark_info, watermark_vmo, kImageFormatTableSize,
                         &duplicated_watermark_info);
  status = task->InitWatermark(nullptr, nullptr, duplicated_watermark_info.data(),
                               image_format_table, kImageFormatTableSize, 0, &frame_callback,
                               &res_callback, &remove_task_callback, bti_handle, fake_canvas);
  EXPECT_NE(ZX_OK, status);
}

}  // namespace
}  // namespace ge2d
