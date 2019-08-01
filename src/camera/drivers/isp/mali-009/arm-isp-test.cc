// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp-test.h"

#include <fuchsia/camera/test/c/fidl.h>
#include <zircon/fidl.h>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "arm-isp.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace camera {

zx_status_t ArmIspDeviceTester::Create(ArmIspDevice* isp, fit::callback<void()>* on_isp_unbind) {
  fbl::AllocChecker ac;
  auto isp_test_device = fbl::make_unique_checked<ArmIspDeviceTester>(&ac, isp, isp->zxdev());
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Unable to start ArmIspDeviceTester \n", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  *on_isp_unbind = fit::bind_member(isp_test_device.get(), &ArmIspDeviceTester::Disconnect);

  zx_status_t status = isp_test_device->DdkAdd("arm-isp-tester");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create arm-isp-tester device: %d\n", status);
    return status;
  } else {
    zxlogf(INFO, "arm-isp: Added arm-isp-tester device\n");
  }

  // isp_test_device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = isp_test_device.release();

  return status;
}

// Methods required by the ddk.
void ArmIspDeviceTester::DdkRelease() { delete this; }

void ArmIspDeviceTester::DdkUnbind() { DdkRemove(); }

void ArmIspDeviceTester::Disconnect() {
  fbl::AutoLock guard(&isp_lock_);
  isp_ = nullptr;
}

zx_status_t ArmIspDeviceTester::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_camera_test_IspTester_dispatch(this, txn, msg, &isp_tester_ops);
}
struct FrameReadyReceiver {
  fbl::Vector<uint32_t> ready_ids;
  void FrameReady(uint32_t buffer_id) { ready_ids.push_back(buffer_id); }
  output_stream_callback GetCallback() {
    output_stream_callback cb;
    cb.ctx = this;
    cb.frame_ready = [](void* ctx, uint32_t buffer_id) {
      reinterpret_cast<FrameReadyReceiver*>(ctx)->FrameReady(buffer_id);
    };
    return cb;
  }
};

// These macros provide a similar style to our normal zxtest/gtests,
// but it actually do something much different under the hood.
// Instead of registering with a gtest framework, the results are summed
// by a local variable called |reports|, which is later passed back over
// a fidl interface to the test binary.
// TODO(CAM-82): Use these macros more widely in this class, and add a
// TEST() macro to deal with the report management.
#define ISP_TEST_ASSERT_OK(expr, msg)     \
  report->test_count++;                   \
  if ((expr) == ZX_OK) {                  \
    report->success_count++;              \
  } else {                                \
    report->failure_count++;              \
    zxlogf(ERROR, "[FAILURE] %s\n", msg); \
    return;                               \
  }

#define ISP_TEST_EXPECT_OK(expr, msg)     \
  report->test_count++;                   \
  if ((expr) == ZX_OK) {                  \
    report->success_count++;              \
  } else {                                \
    report->failure_count++;              \
    zxlogf(ERROR, "[FAILURE] %s\n", msg); \
  }

#define ISP_TEST_EXPECT_NOT_OK(expr, msg) \
  report->test_count++;                   \
  if ((expr) != ZX_OK) {                  \
    report->success_count++;              \
  } else {                                \
    report->failure_count++;              \
    zxlogf(ERROR, "[FAILURE] %s\n", msg); \
  }

#define ISP_TEST_EXPECT_EQ(expr1, expr2, msg) \
  report->test_count++;                       \
  if ((expr1) == (expr2)) {                   \
    report->success_count++;                  \
  } else {                                    \
    report->failure_count++;                  \
    zxlogf(ERROR, "[FAILURE] %s\n", msg);     \
  }

#define ISP_TEST_EXPECT_GT(expr1, expr2, msg) \
  report->test_count++;                       \
  if ((expr1) > (expr2)) {                    \
    report->success_count++;                  \
  } else {                                    \
    report->failure_count++;                  \
    zxlogf(ERROR, "[FAILURE] %s\n", msg);     \
  }

#define ISP_TEST_ASSERT_EQ(expr1, expr2, msg) \
  report->test_count++;                       \
  if ((expr1) == (expr2)) {                   \
    report->success_count++;                  \
  } else {                                    \
    report->failure_count++;                  \
    zxlogf(ERROR, "[FAILURE] %s\n", msg);     \
    return;                                   \
  }

void ArmIspDeviceTester::TestWriteRegister(fuchsia_camera_test_TestReport* report) {
  // We'll enable then disable the global debug register:
  fbl::AutoLock guard(&isp_lock_);
  uint32_t offset = IspGlobalDbg::Get().addr() / 4;  // divide by 4 to get the word address.
  IspGlobalDbg::Get().ReadFrom(&(isp_->isp_mmio_)).set_mode_en(1).WriteTo(&(isp_->isp_mmio_));
  ArmIspRegisterDump after_enable = isp_->DumpRegisters();
  ISP_TEST_EXPECT_EQ(after_enable.global_config[offset], 1, "Global debug was not enabled!");
  IspGlobalDbg::Get().ReadFrom(&(isp_->isp_mmio_)).set_mode_en(0).WriteTo(&(isp_->isp_mmio_));
  ArmIspRegisterDump after_disable = isp_->DumpRegisters();
  ISP_TEST_EXPECT_EQ(after_disable.global_config[offset], 0, "Global debug was not disabled!");
}

void ArmIspDeviceTester::TestConnectStream(fuchsia_camera_test_TestReport* report) {
  constexpr uint32_t kWidth = 1080;
  constexpr uint32_t kHeight = 764;
  constexpr uint32_t kNumberOfBuffers = 8;
  fuchsia_camera_common_FrameRate rate = {.frames_per_sec_numerator = 30,
                                          .frames_per_sec_denominator = 1};
  FrameReadyReceiver receiver;
  output_stream_callback cb = receiver.GetCallback();
  output_stream_protocol output_stream;
  output_stream_protocol_ops_t ops;
  output_stream.ops = &ops;

  fuchsia_sysmem_BufferCollectionInfo buffer_collection;
  fbl::AutoLock guard(&isp_lock_);
  ISP_TEST_ASSERT_OK(camera::CreateContiguousBufferCollectionInfo(
                         &buffer_collection, isp_->bti_.get(), kWidth, kHeight, kNumberOfBuffers),
                     "Failed to create contiguous buffers");

  ISP_TEST_EXPECT_OK(isp_->IspCreateOutputStream(&buffer_collection, &rate,
                                                 STREAM_TYPE_FULL_RESOLUTION, &cb, &output_stream),
                     "Failed to create full resolution input stream");

  ISP_TEST_EXPECT_OK(isp_->IspCreateOutputStream(&buffer_collection, &rate, STREAM_TYPE_DOWNSCALED,
                                                 &cb, &output_stream),
                     "Failed to create downscaled input stream");

  ISP_TEST_EXPECT_EQ(isp_->IspCreateOutputStream(&buffer_collection, &rate, STREAM_TYPE_SCALAR, &cb,
                                                 &output_stream),
                     ZX_ERR_NOT_SUPPORTED,
                     "Failed to return ZX_ERR_NOT_SUPPORTED for scalar stream");

  ISP_TEST_EXPECT_EQ(isp_->IspCreateOutputStream(&buffer_collection, &rate, STREAM_TYPE_INVALID,
                                                 &cb, &output_stream),
                     ZX_ERR_INVALID_ARGS,
                     "Failed to return ZX_ERR_INVALID_ARGS for invalid stream");
}

void ArmIspDeviceTester::TestCallbacks(fuchsia_camera_test_TestReport* report) {
  constexpr uint32_t kWidth = 1920;
  constexpr uint32_t kHeight = 1080;
  constexpr uint32_t kFramesToSleep = 5;
  constexpr uint32_t kNumberOfBuffers = 8;
  fuchsia_camera_common_FrameRate rate = {.frames_per_sec_numerator = 30,
                                          .frames_per_sec_denominator = 1};
  FrameReadyReceiver full_res_receiver;
  FrameReadyReceiver downscaled_receiver;
  output_stream_callback full_res_cb = full_res_receiver.GetCallback();
  output_stream_protocol full_res_output_stream, downscaled_output_stream;
  output_stream_protocol_ops_t full_res_ops, downscaled_ops;
  full_res_output_stream.ops = &full_res_ops;
  downscaled_output_stream.ops = &downscaled_ops;

  fuchsia_sysmem_BufferCollectionInfo buffer_collection;
  fbl::AutoLock guard(&isp_lock_);
  ISP_TEST_ASSERT_OK(camera::CreateContiguousBufferCollectionInfo(
                         &buffer_collection, isp_->bti_.get(), kWidth, kHeight, kNumberOfBuffers),
                     "Failed to create contiguous buffers.");

  ISP_TEST_EXPECT_OK(
      isp_->IspCreateOutputStream(&buffer_collection, &rate, STREAM_TYPE_FULL_RESOLUTION,
                                  &full_res_cb, &full_res_output_stream),
      "Failed to create full resolution input stream.");

  output_stream_callback downscaled_cb = downscaled_receiver.GetCallback();
  ISP_TEST_EXPECT_OK(isp_->IspCreateOutputStream(&buffer_collection, &rate, STREAM_TYPE_DOWNSCALED,
                                                 &downscaled_cb, &downscaled_output_stream),
                     "Failed to create downscaled input stream.");

  // Try to release a frame before things are started.  Should fail.
  ISP_TEST_EXPECT_NOT_OK(full_res_output_stream.ops->release_frame(full_res_output_stream.ctx, 0),
                         "Unexpected success from releasing un-started full resolution stream.");
  ISP_TEST_EXPECT_NOT_OK(
      downscaled_output_stream.ops->release_frame(downscaled_output_stream.ctx, 0),
      "Unexpected success from releasing un-started downscaled stream.");

  // Manually cycle dma through one frame, see that callbacks are called
  isp_->full_resolution_dma_->Enable();
  isp_->full_resolution_dma_->OnNewFrame();
  isp_->full_resolution_dma_->OnFrameWritten();
  ISP_TEST_EXPECT_EQ(full_res_receiver.ready_ids.size(), 1,
                     "Full resolution callbacks not equal to 1.");
  ISP_TEST_EXPECT_EQ(downscaled_receiver.ready_ids.size(), 0,
                     "Downscaled callbacks not equal to 0.");
  isp_->downscaled_dma_->Enable();
  isp_->downscaled_dma_->OnNewFrame();
  isp_->downscaled_dma_->OnFrameWritten();
  ISP_TEST_ASSERT_EQ(full_res_receiver.ready_ids.size(), 1,
                     "Full resolution callbacks not equal to 1.");
  ISP_TEST_ASSERT_EQ(downscaled_receiver.ready_ids.size(), 1,
                     "Downscaled callbacks not equal to 1.");

  // Release frame should now work
  ISP_TEST_EXPECT_OK(full_res_output_stream.ops->release_frame(full_res_output_stream.ctx,
                                                               full_res_receiver.ready_ids[0]),
                     "Failed to release frame from full resolution stream.");
  ISP_TEST_EXPECT_OK(downscaled_output_stream.ops->release_frame(downscaled_output_stream.ctx,
                                                                 downscaled_receiver.ready_ids[0]),
                     "Failed to release frame from downscaled stream.");

  // Now call start.  The ISP should start processing frames, and we should
  // start getting callbacks.  This tests the whole pipeline!
  // TODO(CAM-91): Enable the test patterns so we can limit testing here to just
  // the ISP.
  isp_->full_resolution_dma_->Disable();
  isp_->downscaled_dma_->Disable();
  ISP_TEST_EXPECT_OK(full_res_output_stream.ops->start(full_res_output_stream.ctx),
                     "Failed to start streaming.");
  // sleep for kFramesToSleep frame periods.
  auto frame_period_ms = 1000 * rate.frames_per_sec_denominator / rate.frames_per_sec_numerator;
  zx_nanosleep(zx_deadline_after(ZX_MSEC(kFramesToSleep * frame_period_ms)));
  ISP_TEST_EXPECT_GT(full_res_receiver.ready_ids.size(), 1,
                     "Full res callbacks not increasing past 1. Additional callbacks "
                     "have not been received.");
  ISP_TEST_EXPECT_EQ(downscaled_receiver.ready_ids.size(), 1,
                     "Downscaled callbacks have not remained equal to 1.");
  // When we stop the stream, no further callbacks should be received.
  ISP_TEST_EXPECT_OK(full_res_output_stream.ops->stop(full_res_output_stream.ctx),
                     "Failed to stop streaming.");
  auto callback_count_at_stop = full_res_receiver.ready_ids.size();
  // now sleep for a while to make sure we did not get any more callbacks after
  // stopping the stream:
  ISP_TEST_ASSERT_EQ(full_res_receiver.ready_ids.size(), callback_count_at_stop,
                     "Full res callbacks increased after stop was called");
  isp_->full_resolution_dma_->Disable();
  isp_->downscaled_dma_->Disable();
}

// DDKMessage Helper Functions.
zx_status_t ArmIspDeviceTester::RunTests(fidl_txn_t* txn) {
  fuchsia_camera_test_TestReport report = {1, 0, 0};
  {
    fbl::AutoLock guard(&isp_lock_);
    if (!isp_) {
      return ZX_ERR_BAD_STATE;
    }
    if (isp_->RunTests() == ZX_OK) {
      report.success_count++;
    } else {
      report.failure_count++;
    }
  }
  TestWriteRegister(&report);
  TestConnectStream(&report);
  TestCallbacks(&report);
  return fuchsia_camera_test_IspTesterRunTests_reply(txn, ZX_OK, &report);
}

}  // namespace camera
