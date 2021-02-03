/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"

namespace wlan::brcmfmac {

static const struct brcmf_bus_ops brcmf_test_bus_ops = {
    .get_bus_type = []() { return BRCMF_BUS_TYPE_SDIO; },
};

class RecoveryTriggerTest : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  // The first worker handler function is added into the WorkQueue by a successfully triggered
  // recovery process.
  static void TestRecoveryWorker(WorkItem* work_item);
  // The dummy worker function is added into the WorkQueue by a test case, if this handler function
  // is executed, but the boolean "recovery_trigger_" is false, we know that the recovery worker was
  // not added into WorkQueue.
  static void TestRecoveryDummyWorker(WorkItem* work_item);
  zx_status_t WaitForTrigger(uint32_t delay);
  zx_status_t WaitForDummy(uint32_t delay);
  // Clear the states of all async protections variable to ensure next trigger's success.
  void ResetAsync();

  std::unique_ptr<brcmf_bus> bus_if_;
  // Test object
  std::unique_ptr<RecoveryTrigger> trigger_;

  static uint16_t recovery_trigger_count_;
  // Mark that the dummy worker is executed.
  static bool recovery_not_triggered_;
  static sync_completion_t wait_for_worker_;
  static sync_completion_t wait_for_dummy_worker_;

 private:
  std::unique_ptr<brcmf_pub> fake_drvr_;
};

uint16_t RecoveryTriggerTest::recovery_trigger_count_ = 0;
bool RecoveryTriggerTest::recovery_not_triggered_ = false;
sync_completion_t RecoveryTriggerTest::wait_for_worker_;
sync_completion_t RecoveryTriggerTest::wait_for_dummy_worker_;

void RecoveryTriggerTest::TestRecoveryWorker(WorkItem* work_item) {
  recovery_trigger_count_++;
  sync_completion_signal(&wait_for_worker_);
}

void RecoveryTriggerTest::TestRecoveryDummyWorker(WorkItem* work_item) {
  recovery_not_triggered_ = true;
  sync_completion_signal(&wait_for_dummy_worker_);
}

zx_status_t RecoveryTriggerTest::WaitForTrigger(uint32_t delay) {
  return sync_completion_wait(&wait_for_worker_, delay);
}

zx_status_t RecoveryTriggerTest::WaitForDummy(uint32_t delay) {
  return sync_completion_wait(&wait_for_dummy_worker_, delay);
}

void RecoveryTriggerTest::ResetAsync() {
  fake_drvr_->drvr_resetting.store(false);
  sync_completion_reset(&wait_for_worker_);
  sync_completion_reset(&wait_for_dummy_worker_);
}

void RecoveryTriggerTest::SetUp() {
  fake_drvr_ = std::make_unique<brcmf_pub>();

  // Set the bus type to BRCMF_BUS_TYPE_SIM to let the worker to be directly executed.
  bus_if_ = std::make_unique<brcmf_bus>();
  bus_if_->ops = &brcmf_test_bus_ops;
  fake_drvr_->bus_if = bus_if_.get();

  // Create WorkItem for the entry point worker of the recovery process.
  fake_drvr_->recovery_work = WorkItem(RecoveryTriggerTest::TestRecoveryWorker);

  // Initialize RecoveryTrigger class, note that this test includes the WorkQueue workflow to ensure
  // the cooperation between RecoveryTrigger and WorkQueue is going weLl.
  auto recovery_start_callback = std::make_shared<std::function<zx_status_t()>>();
  *recovery_start_callback = std::bind(&brcmf_schedule_recovery_worker, fake_drvr_.get());
  trigger_ = std::make_unique<RecoveryTrigger>(recovery_start_callback);
}

void RecoveryTriggerTest::TearDown() {
  // Resetting the recovery trigger flag.
  RecoveryTriggerTest::recovery_trigger_count_ = 0;
  RecoveryTriggerTest::recovery_not_triggered_ = false;
  ResetAsync();
}

TEST_F(RecoveryTriggerTest, SdioTimeoutTriggerTest) {
  for (uint16_t i = 0; i < RecoveryTrigger::kSdioTimeoutThreshold; i++) {
    EXPECT_EQ(trigger_->sdio_timeout_.Inc(), ZX_OK);
  }
  zx_status_t status = WaitForTrigger(ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(RecoveryTriggerTest::recovery_trigger_count_, 1U);
}

TEST_F(RecoveryTriggerTest, SdioTimeoutTriggerNegative) {
  for (uint16_t i = 0; i < RecoveryTrigger::kSdioTimeoutThreshold - 1; i++) {
    EXPECT_EQ(trigger_->sdio_timeout_.Inc(), ZX_OK);
  }
  trigger_->ClearStatistics();
  EXPECT_EQ(trigger_->sdio_timeout_.Inc(), ZX_OK);

  // Schedule dummy worker to the default WorkQueue.
  WorkItem dummy_worker(RecoveryTriggerTest::TestRecoveryDummyWorker);
  WorkQueue::ScheduleDefault(&dummy_worker);
  zx_status_t status = WaitForDummy(ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(RecoveryTriggerTest::recovery_not_triggered_);
  EXPECT_EQ(RecoveryTriggerTest::recovery_trigger_count_, 0U);
}

// This test case verifies that the firmware_crash_ TriggerCondition can be successfully triggered,
// and increasing counter after threshold is reached will not trigger another recovery callback.
TEST_F(RecoveryTriggerTest, FirmwareCrashTriggerTest) {
  EXPECT_EQ(trigger_->firmware_crash_.Inc(), ZX_OK);
  zx_status_t status = WaitForTrigger(ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(RecoveryTriggerTest::recovery_trigger_count_, 1U);
  ResetAsync();

  // The counter will not be cleared in the test callback, so next Inc() for this
  // TriggerCondition will hit the over_threshold_ bool.
  EXPECT_EQ(trigger_->firmware_crash_.Inc(), ZX_ERR_BAD_STATE);
  EXPECT_EQ(RecoveryTriggerTest::recovery_trigger_count_, 1U);
  ResetAsync();

  // The recovery process can be triggered again after the over_threshold_ bool is cleared.
  trigger_->firmware_crash_.Clear();
  EXPECT_EQ(trigger_->firmware_crash_.Inc(), ZX_OK);
  status = WaitForTrigger(ZX_TIME_INFINITE);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(RecoveryTriggerTest::recovery_trigger_count_, 2U);
}

TEST_F(RecoveryTriggerTest, NoCallbackFunction) {
  RecoveryTrigger no_callback_trigger(nullptr);
  EXPECT_EQ(no_callback_trigger.firmware_crash_.Inc(), ZX_ERR_NOT_FOUND);
}

}  // namespace wlan::brcmfmac
