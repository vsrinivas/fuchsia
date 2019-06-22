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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

namespace {

class PcieTest;

struct iwl_trans_pcie_wrapper {
  struct iwl_trans_pcie trans_pcie;
  PcieTest* test;
};

void write32_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val);

class TransOps {
public:
  virtual void write32(uint32_t ofs, uint32_t val) = 0;
};

class PcieTest : public ::testing::Test, TransOps {
 public:
  PcieTest() {
    trans_ops_.write32 = write32_wrapper;
    trans_ = iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &cfg_, &trans_ops_);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
  }

  ~PcieTest() {
    iwl_trans_free(trans_);
  }

  MOCK_METHOD2(write32, void(uint32_t ofs, uint32_t val));

 protected:
  struct iwl_trans* trans_;
  struct iwl_trans_pcie* trans_pcie_;
  struct iwl_cfg cfg_;
  struct iwl_trans_ops trans_ops_;
};

void write32_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
  auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
  wrapper->test->write32(ofs, val);
}

TEST_F(PcieTest, DisableInterrupts) {
  ASSERT_NE(trans_, nullptr);

  trans_pcie_->msix_enabled = false;
  set_bit(STATUS_INT_ENABLED, &trans_->status);

  EXPECT_CALL(*this, write32(CSR_INT_MASK, 0x00000000));
  EXPECT_CALL(*this, write32(CSR_INT, 0xffffffff));
  EXPECT_CALL(*this, write32(CSR_FH_INT_STATUS, 0xffffffff));
  iwl_disable_interrupts(trans_);
  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
}

TEST_F(PcieTest, DisableInterruptsMsix) {
  ASSERT_NE(trans_, nullptr);

  trans_pcie_->msix_enabled = true;
  trans_pcie_->fh_init_mask = 0xabab;
  trans_pcie_->hw_init_mask = 0x4242;
  set_bit(STATUS_INT_ENABLED, &trans_->status);

  EXPECT_CALL(*this, write32(CSR_MSIX_FH_INT_MASK_AD, 0xabab));
  EXPECT_CALL(*this, write32(CSR_MSIX_HW_INT_MASK_AD, 0x4242));
  iwl_disable_interrupts(trans_);
  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
}

}  // namespace
