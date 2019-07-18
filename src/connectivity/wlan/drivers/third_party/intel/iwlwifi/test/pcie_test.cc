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

#include <stdio.h>

#include <lib/mock-function/mock-function.h>
#include <zxtest/zxtest.h>

extern "C" {
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
    trans_ops_.write32 = write32_wrapper;
    trans_ = iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &cfg_, &trans_ops_);
    ASSERT_NE(trans_, nullptr);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
  }

  ~PcieTest() { iwl_trans_free(trans_); }

  mock_function::MockFunction<void, uint32_t, uint32_t> mock_write32_;
  static void write32_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    wrapper->test->mock_write32_.Call(ofs, val);
  }

 protected:
  struct iwl_trans* trans_;
  struct iwl_trans_pcie* trans_pcie_;
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

}  // namespace
