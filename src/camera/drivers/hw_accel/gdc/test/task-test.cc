// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../task.h"

#include <ddk/debug.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/syslog/global.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <stdint.h>
#include <unistd.h>
#include <zxtest/zxtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../gdc.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace gdc {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumberOfBuffers = 5;
constexpr uint32_t kNumberOfMmios = 50;
constexpr uint32_t kConfigSize = 1000;
constexpr uint32_t kMaxTasks = 10;

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class TaskTest : public zxtest::Test {
 public:
  void ProcessFrameCallback(uint32_t output_buffer_id) {
    callback_check_.push_back(output_buffer_id);
  }

 protected:
  void SetUpBufferCollections(uint32_t buffer_collection_count) {
    ASSERT_OK(fake_bti_create(bti_handle_.reset_and_get_address()));

    zx_status_t status = camera::CreateContiguousBufferCollectionInfo(
        &input_buffer_collection_, bti_handle_.get(), kWidth, kHeight, buffer_collection_count);
    ASSERT_OK(status);

    status = camera::CreateContiguousBufferCollectionInfo(
        &output_buffer_collection_, bti_handle_.get(), kWidth, kHeight, buffer_collection_count);
    ASSERT_OK(status);

    status = zx_vmo_create_contiguous(bti_handle_.get(), kConfigSize, 0,
                                      config_vmo_.reset_and_get_address());
    ASSERT_OK(status);
  }

  // Sets up GdcDevice, initialize a task.
  // Returns a task id.
  uint32_t SetupForFrameProcessing() {
    SetUpBufferCollections(kNumberOfBuffers);
    ddk_mock::MockMmioReg fake_reg_array[kNumberOfMmios];
    ddk_mock::MockMmioRegRegion fake_regs(fake_reg_array, sizeof(uint32_t), kNumberOfMmios);

    zx::port port;
    EXPECT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

    callback_.frame_ready = [](void* ctx, uint32_t idx) {
      return static_cast<TaskTest*>(ctx)->ProcessFrameCallback(idx);
    };
    callback_.ctx = this;

    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    EXPECT_OK(port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_));

    zx::interrupt irq;
    EXPECT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq));

    gdc_device_ =
        std::make_unique<GdcDevice>(nullptr, ddk::MmioBuffer(fake_regs.GetMmioBuffer()),
                                    ddk::MmioBuffer(fake_regs.GetMmioBuffer()), std::move(irq),
                                    std::move(bti_handle_), std::move(port));

    zx::vmo config_vmo;
    EXPECT_OK(config_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &config_vmo));
    uint32_t task_id;
    zx_status_t status =
        gdc_device_->GdcInitTask(&input_buffer_collection_, &output_buffer_collection_,
                                 std::move(config_vmo), &callback_, &task_id);
    EXPECT_OK(status);

    // Start the thread.
    EXPECT_OK(gdc_device_->StartThread());

    return task_id;
  }

  void TearDown() override {
    if (bti_handle_ != ZX_HANDLE_INVALID) {
      fake_bti_destroy(bti_handle_.get());
    }
  }

  zx::vmo config_vmo_;
  zx::bti bti_handle_;
  zx::port port_;
  zx::interrupt irq_;
  gdc_callback_t callback_;
  buffer_collection_info_t input_buffer_collection_;
  buffer_collection_info_t output_buffer_collection_;
  std::unique_ptr<GdcDevice> gdc_device_;
  std::vector<uint32_t> callback_check_;
};

TEST_F(TaskTest, BasicCreationTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  std::unique_ptr<Task> task;
  zx_status_t status = gdc::Task::Create(&input_buffer_collection_, &output_buffer_collection_,
                                         config_vmo_, &callback_, bti_handle_, &task);
  EXPECT_OK(status);
}

TEST_F(TaskTest, InvalidFormatTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  std::unique_ptr<Task> task;

  input_buffer_collection_.format.image.pixel_format.type = fuchsia_sysmem_PixelFormatType_YUY2;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            gdc::Task::Create(&input_buffer_collection_, &output_buffer_collection_, config_vmo_,
                              &callback_, bti_handle_, &task));
}

TEST_F(TaskTest, InputBufferTest) {
  SetUpBufferCollections(kNumberOfBuffers);
  std::unique_ptr<Task> task;
  zx_status_t status = gdc::Task::Create(&input_buffer_collection_, &output_buffer_collection_,
                                         config_vmo_, &callback_, bti_handle_, &task);
  EXPECT_OK(status);

  // Get the input buffers physical addresses
  // Request for physical addresses of the buffers.
  // Expecting to get error when requesting for invalid index.
  zx_paddr_t addr;
  for (uint32_t i = 0; i < kNumberOfBuffers; i++) {
    EXPECT_EQ(ZX_OK, task->GetInputBufferPhysAddr(0, &addr));
  }

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, task->GetInputBufferPhysAddr(kNumberOfBuffers, &addr));
}

TEST_F(TaskTest, InvalidVmoTest) {
  SetUpBufferCollections(0);

  std::unique_ptr<Task> task;
  zx_status_t status = gdc::Task::Create(&input_buffer_collection_, &output_buffer_collection_,
                                         config_vmo_, &callback_, bti_handle_, &task);
  // Expecting Task setup to be returning an error when there are
  // no VMOs in the buffer collection. At the moment VmoPool library
  // doesn't return an error.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST_F(TaskTest, InitTaskTest) {
  __UNUSED auto task_id = SetupForFrameProcessing();

  std::vector<uint32_t> received_ids;
  for (uint32_t i = 0; i < kMaxTasks; i++) {
    zx::vmo config_vmo;
    ASSERT_OK(config_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &config_vmo));

    uint32_t task_id;
    zx_status_t status =
        gdc_device_->GdcInitTask(&input_buffer_collection_, &output_buffer_collection_,
                                 std::move(config_vmo), &callback_, &task_id);
    EXPECT_OK(status);
    // Checking to see if we are getting unique task ids.
    auto entry = find(received_ids.begin(), received_ids.end(), task_id);
    EXPECT_EQ(received_ids.end(), entry);
    received_ids.push_back(task_id);
  }
}

TEST_F(TaskTest, RemoveTaskTest) {
  auto task_id = SetupForFrameProcessing();

  // Valid id.
  ASSERT_NO_DEATH(([this, task_id]() { gdc_device_->GdcRemoveTask(task_id); }));

  // Invalid id.
  ASSERT_DEATH(([this, task_id]() { gdc_device_->GdcRemoveTask(task_id + 1); }));
}

TEST_F(TaskTest, ProcessInvalidFrameTest) {
  __UNUSED auto task_id = SetupForFrameProcessing();

  // Invalid task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(0xFF, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, InvalidBufferProcessFrameTest) {
  __UNUSED auto task_id = SetupForFrameProcessing();

  // Invalid buffer id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ProcessFrameTest) {
  auto task_id = SetupForFrameProcessing();

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(1, callback_check_.size());

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ReleaseValidFrameTest) {
  auto task_id = SetupForFrameProcessing();

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(1, callback_check_.size());

  // Release the output buffer index provided as callback.
  ASSERT_NO_DEATH(
      ([this, task_id]() { gdc_device_->GdcReleaseFrame(task_id, callback_check_.back()); }));

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, ReleaseInValidFrameTest) {
  auto task_id = SetupForFrameProcessing();

  // Valid buffer & task id.
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(1, callback_check_.size());

  // Release the output buffer index provided as callback.
  ASSERT_DEATH(
      ([this, task_id]() { gdc_device_->GdcReleaseFrame(task_id + 1, callback_check_.back()); }));

  ASSERT_OK(gdc_device_->StopThread());
}

TEST_F(TaskTest, MultipleProcessFrameTest) {
  auto task_id = SetupForFrameProcessing();

  // Process few frames, putting them in a queue
  zx_status_t status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 1);
  EXPECT_OK(status);
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 2);
  EXPECT_OK(status);
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 3);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  zx_port_packet packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called once.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(1, callback_check_.size());

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(2, callback_check_.size());

  // This time adding another frame to process while its
  // waiting for an interrupt.
  status = gdc_device_->GdcProcessFrame(task_id, kNumberOfBuffers - 3);
  EXPECT_OK(status);

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(3, callback_check_.size());

  // Trigger the interrupt manually.
  packet = {kPortKeyDebugFakeInterrupt, ZX_PKT_TYPE_USER, ZX_OK, {}};
  EXPECT_OK(port_.queue(&packet));

  // Check if the callback was called one more time.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  EXPECT_EQ(4, callback_check_.size());

  ASSERT_OK(gdc_device_->StopThread());
}

TEST(TaskTest, NonContigVmoTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  gdc_callback_t callback;
  zx_handle_t config_vmo;
  buffer_collection_info_t input_buffer_collection;
  buffer_collection_info_t output_buffer_collection;
  ASSERT_OK(fake_bti_create(&bti_handle));

  zx_status_t status = camera::CreateContiguousBufferCollectionInfo(&input_buffer_collection,
                                                                    bti_handle, kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = camera::CreateContiguousBufferCollectionInfo(&output_buffer_collection, bti_handle,
                                                        kWidth, kHeight, 0);
  ASSERT_OK(status);

  status = zx_vmo_create(kConfigSize, 0, &config_vmo);
  ASSERT_OK(status);

  std::unique_ptr<Task> task;
  status = gdc::Task::Create(&input_buffer_collection, &output_buffer_collection,
                             zx::vmo(config_vmo), &callback, zx::bti(bti_handle), &task);
  // Expecting Task setup to be returning an error when config vmo is not
  // contig.
  EXPECT_NE(ZX_OK, status);
}

TEST(TaskTest, InvalidBufferCollectionTest) {
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  gdc_callback_t callback;
  zx_handle_t config_vmo;
  ASSERT_OK(fake_bti_create(&bti_handle));

  zx_status_t status = zx_vmo_create_contiguous(bti_handle, kConfigSize, 0, &config_vmo);
  ASSERT_OK(status);

  std::unique_ptr<Task> task;
  status = gdc::Task::Create(nullptr, nullptr, zx::vmo(config_vmo), &callback, zx::bti(bti_handle),
                             &task);
  EXPECT_NE(ZX_OK, status);
}

}  // namespace
}  // namespace gdc
