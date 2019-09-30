/*
 * Copyright (c) 2019 The Fuchsia Authors
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

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fake-bti/bti.h>
#include <lib/mock-function/mock-function.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <zircon/listnode.h>

#include <thread>

#include <ddk/io-buffer.h>
#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

namespace {

class PcieTest;

struct iwl_trans_pcie_wrapper {
  struct iwl_trans_pcie trans_pcie;
  PcieTest* test;
};

class PcieTest : public zxtest::Test {
 public:
  PcieTest() {
    trans_ops_.write8 = write8_wrapper;
    trans_ops_.write32 = write32_wrapper;
    trans_ops_.read32 = read32_wrapper;
    trans_ops_.grab_nic_access = grab_nic_access_wrapper;
    trans_ops_.release_nic_access = release_nic_access_wrapper;
    cfg_.base_params = &base_params_;
    trans_ = iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &cfg_, &trans_ops_);
    ASSERT_NE(trans_, nullptr);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
    fake_bti_create(&trans_pcie_->bti);
  }

  ~PcieTest() {
    fake_bti_destroy(trans_pcie_->bti);
    iwl_trans_free(trans_);
  }

  mock_function::MockFunction<void, uint32_t, uint8_t> mock_write8_;
  static void write8_wrapper(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write8_.HasExpectations()) {
      wrapper->test->mock_write8_.Call(ofs, val);
    }
  }

  mock_function::MockFunction<void, uint32_t, uint32_t> mock_write32_;
  static void write32_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write32_.HasExpectations()) {
      wrapper->test->mock_write32_.Call(ofs, val);
    }
  }

  mock_function::MockFunction<uint32_t, uint32_t> mock_read32_;
  static uint32_t read32_wrapper(struct iwl_trans* trans, uint32_t ofs) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_read32_.HasExpectations()) {
      return wrapper->test->mock_read32_.Call(ofs);
    } else {
      return 0;
    }
  }

  mock_function::MockFunction<bool, unsigned long*> mock_grab_nic_access_;
  static bool grab_nic_access_wrapper(struct iwl_trans* trans, unsigned long* flags) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_grab_nic_access_.HasExpectations()) {
      return wrapper->test->mock_grab_nic_access_.Call(flags);
    } else {
      return true;
    }
  }

  mock_function::MockFunction<void, unsigned long*> release_nic_access_;
  static void release_nic_access_wrapper(struct iwl_trans* trans, unsigned long* flags) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->release_nic_access_.HasExpectations()) {
      wrapper->test->release_nic_access_.Call(flags);
    }
  }

 protected:
  struct iwl_trans* trans_;
  struct iwl_trans_pcie* trans_pcie_;
  struct iwl_base_params base_params_;
  struct iwl_cfg cfg_;
  struct iwl_trans_ops trans_ops_;
};

TEST_F(PcieTest, DisableInterrupts) {
  mock_write32_.ExpectCall(CSR_INT_MASK, 0x00000000);
  mock_write32_.ExpectCall(CSR_INT, 0xffffffff);
  mock_write32_.ExpectCall(CSR_FH_INT_STATUS, 0xffffffff);

  trans_pcie_->msix_enabled = false;
  set_bit(STATUS_INT_ENABLED, &trans_->status);
  iwl_disable_interrupts(trans_);

  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
  mock_write32_.VerifyAndClear();
}

TEST_F(PcieTest, DisableInterruptsMsix) {
  mock_write32_.ExpectCall(CSR_MSIX_FH_INT_MASK_AD, 0xabab);
  mock_write32_.ExpectCall(CSR_MSIX_HW_INT_MASK_AD, 0x4242);

  trans_pcie_->msix_enabled = true;
  trans_pcie_->fh_init_mask = 0xabab;
  trans_pcie_->hw_init_mask = 0x4242;
  set_bit(STATUS_INT_ENABLED, &trans_->status);
  iwl_disable_interrupts(trans_);

  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
  mock_write32_.VerifyAndClear();
}

TEST_F(PcieTest, RxInit) {
  trans_->num_rx_queues = 1;
  trans_pcie_->rx_buf_size = IWL_AMSDU_2K;
  ASSERT_OK(iwl_pcie_rx_init(trans_));

  EXPECT_EQ(trans_pcie_->rxq->read, 0);
  EXPECT_EQ(trans_pcie_->rxq->write, 0);
  EXPECT_EQ(trans_pcie_->rxq->free_count, RX_QUEUE_SIZE);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 0);
  EXPECT_EQ(trans_pcie_->rxq->queue_size, RX_QUEUE_SIZE);
  EXPECT_GE(list_length(&trans_pcie_->rxq->rx_free), RX_QUEUE_SIZE);
  EXPECT_TRUE(io_buffer_is_valid(&trans_pcie_->rxq->rb_status));

  struct iwl_rx_mem_buffer* rxb;
  list_for_every_entry (&trans_pcie_->rxq->rx_free, rxb, struct iwl_rx_mem_buffer, list) {
    EXPECT_TRUE(io_buffer_is_valid(&rxb->io_buf));
    EXPECT_EQ(io_buffer_size(&rxb->io_buf, 0), 2 * 1024);
  }

  iwl_pcie_rx_free(trans_);
}

TEST_F(PcieTest, IctTable) {
  ASSERT_OK(iwl_pcie_alloc_ict(trans_));

  // Check the initial state.
  ASSERT_TRUE(io_buffer_is_valid(&trans_pcie_->ict_tbl));
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0);
  EXPECT_EQ(trans_pcie_->ict_index, 0);

  // Reads an interrupt from the table.
  uint32_t* ict_table = static_cast<uint32_t*>(io_buffer_virt(&trans_pcie_->ict_tbl));
  trans_pcie_->ict_index = 0;
  ict_table[0] = 0x1234;
  ict_table[1] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0x12000034);
  EXPECT_EQ(trans_pcie_->ict_index, 1);

  // Reads multiple interrupts from the table.
  trans_pcie_->ict_index = 1;
  ict_table[1] = 1 << 0;
  ict_table[2] = 1 << 1;
  ict_table[3] = 1 << 2;
  ict_table[4] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), (1 << 0) | (1 << 1) | (1 << 2));
  EXPECT_EQ(trans_pcie_->ict_index, 4);

  // This should match ICT_COUNT defined in pcie/rx.c.
  size_t ict_count = io_buffer_size(&trans_pcie_->ict_tbl, 0) / sizeof(uint32_t);

  // Guarantee that we have enough room in the table for the tests.
  ASSERT_GT(ict_count, 42);

  // Correctly wraps the index.
  trans_pcie_->ict_index = ict_count - 1;
  ict_table[ict_count - 1] = 1 << 0;
  ict_table[0] = 1 << 1;
  ict_table[1] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), (1 << 0) | (1 << 1));
  EXPECT_EQ(trans_pcie_->ict_index, 1);

  // Hardware bug workaround.
  trans_pcie_->ict_index = 1;
  ict_table[1] = 0xC0000;
  ict_table[2] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0x80000000);

  // Correctly resets
  trans_pcie_->ict_index = 42;
  ict_table[42] = 0xdeadbeef;
  iwl_pcie_reset_ict(trans_);
  EXPECT_EQ(trans_pcie_->ict_index, 0);
  EXPECT_EQ(ict_table[42], 0);

  iwl_pcie_free_ict(trans_);
}

TEST_F(PcieTest, RxInterrupts) {
  trans_->num_rx_queues = 1;
  trans_pcie_->rx_buf_size = IWL_AMSDU_2K;
  trans_pcie_->use_ict = true;
  trans_pcie_->inta_mask = CSR_INI_SET_MASK;
  set_bit(STATUS_DEVICE_ENABLED, &trans_->status);
  ASSERT_OK(iwl_pcie_alloc_ict(trans_));
  ASSERT_OK(iwl_pcie_rx_init(trans_));

  ASSERT_TRUE(io_buffer_is_valid(&trans_pcie_->ict_tbl));
  uint32_t* ict_table = static_cast<uint32_t*>(io_buffer_virt(&trans_pcie_->ict_tbl));

  // Spurious interrupt.
  trans_pcie_->ict_index = 0;
  ict_table[0] = 0;
  ASSERT_OK(iwl_pcie_isr(trans_));

  // Set up the ICT table with an RX interrupt at index 0.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // This struct is controlled by the device, closed_rb_num is the read index for the shared ring
  // buffer. For the tests we manually increment the index to push forward the index.
  struct iwl_rb_status* rb_status =
      static_cast<struct iwl_rb_status*>(io_buffer_virt(&trans_pcie_->rxq->rb_status));

  // Process 128 buffers. The driver should process each buffer and indicate this to the hardware by
  // setting the write index (rxq->write_actual).
  rb_status->closed_rb_num = 128;
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 127);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 120);

  // Reset the interrupt table.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // Process another 300 buffers, wrapping to the beginning of the circular buffer.
  rb_status->closed_rb_num = 172;
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 171);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 168);

  iwl_pcie_rx_free(trans_);
}

class StuckTimerTest : public PcieTest {
 public:
  StuckTimerTest() {
    ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToThread, &trans_->loop));
    ASSERT_OK(async_loop_start_thread(trans_->loop, "iwlwifi-test-worker", NULL));

    mtx_init(&txq_.lock, mtx_plain);
    iwlwifi_timer_init(trans_, &txq_.stuck_timer);

    // Set read and write pointers such that firing the stuck timer is valid.
    txq_.write_ptr = 0;
    txq_.read_ptr = 1;
  }

  void WaitForWorkerThread() {
    async_loop_quit(trans_->loop);
    async_loop_join_threads(trans_->loop);
  }

 protected:
  struct iwl_txq txq_;
};

TEST_F(StuckTimerTest, SetTimer) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, SetSpuriousTimer) {
  // Set read and write pointers such that firing the stuck timer is spurious.
  txq_.write_ptr = 0;
  txq_.read_ptr = 0;
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, SetTimerOverride) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE);
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, StopPendingTimer) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE);
  // Test that stop doesn't deadlock.
  iwlwifi_timer_stop(&txq_.stuck_timer);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, StopUnsetTimer) {
  // Test that stop doesn't deadlock.
  iwlwifi_timer_stop(&txq_.stuck_timer);
  WaitForWorkerThread();
}

// This test is a best-effort attempt to test that a race condition is correctly handled. The test
// should pass both when the race condition is and isn't triggered.
TEST_F(StuckTimerTest, StopRunningTimer) {
  // Hold the txq lock so that the timer handler blocks.
  mtx_lock(&txq_.lock);

  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);

  // Sleep to give the timer thread a chance to schedule.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  // Call stop on a new thread, it will block until the handler completes.
  std::thread stop_thread(iwlwifi_timer_stop, &txq_.stuck_timer);

  // Sleep to give the stop thread a chance to schedule.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  // Unblock the timer thread.
  mtx_unlock(&txq_.lock);

  // Wait for all the threads to finish. This tests that the timer doesn't deadlock. We can't check
  // the value of finished, since there's a chance that the race condition we're trying to trigger
  // didn't happen (i.e. the handler didn't fire before we called stop, or stop didn't block before
  // the handler was unblocked.)
  WaitForWorkerThread();
  stop_thread.join();
}

}  // namespace
