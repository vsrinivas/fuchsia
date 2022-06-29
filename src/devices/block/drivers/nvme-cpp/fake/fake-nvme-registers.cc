// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/fake/fake-nvme-registers.h"

#include <zircon/syscalls.h>

#include "src/devices/block/drivers/nvme-cpp/registers.h"

namespace fake_nvme {

FakeNvmeRegisters::FakeNvmeRegisters() {
  // Pretend to be version 1.4.0.
  vers_.set_major(1).set_minor(4).set_tertiary(0);
  // We emulate a very minimal set of capabilities.
  caps_.set_controller_ready_independent_media_supported(false)
      .set_controller_ready_with_media_supported(true)
      .set_subsystem_shutdown_supported(false)
      .set_controller_memory_buffer_supported(false)
      .set_persistent_memory_region_supported(false)
      .set_memory_page_size_max(__builtin_ctzl(zx_system_get_page_size()) - 12)
      .set_memory_page_size_min(__builtin_ctzl(zx_system_get_page_size()) - 12)
      .set_controller_power_scope(nvme::CapabilityReg::ControllerPowerScope::kNotReported)
      .set_boot_partition_support(false)
      .set_no_io_command_set_support(false)
      .set_identify_io_command_set_support(false)
      .set_nvm_command_set_support(true)
      .set_nvm_subsystem_reset_supported(false)
      .set_doorbell_stride(0)
      .set_timeout(2 /* in 500ms units, so 1s */)
      .set_vendor_specific_arbitration_supported(false)
      .set_weighted_round_robin_arbitration_supported(false)
      .set_contiguous_queues_required(true)
      .set_max_queue_entries_raw(65535);
}

uint64_t FakeNvmeRegisters::Read64(zx_off_t offs) {
  switch (offs) {
    case nvme::NVME_REG_CAP:
      return caps_.reg_value();
    case nvme::NVME_REG_ASQ:
      return admin_submission_queue_.reg_value();
    case nvme::NVME_REG_ACQ:
      return admin_completion_queue_.reg_value();
  }
  // Could be an unsupported register, or just a 32-bit one.
  ZX_ASSERT_MSG(false, "64-bit reads from 0x%lx are not supported", offs);
}

void FakeNvmeRegisters::Write64(uint64_t val, zx_off_t offs) {
  switch (offs) {
    case nvme::NVME_REG_CAP:
      ZX_ASSERT_MSG(false, "CAP register is read-only.");
      return;
    case nvme::NVME_REG_ASQ:
      admin_submission_queue_.set_reg_value(val);
      if (callbacks_) {
        callbacks_->admin_queue_update();
      }
      return;
    case nvme::NVME_REG_ACQ:
      admin_completion_queue_.set_reg_value(val);
      if (callbacks_) {
        callbacks_->admin_queue_update();
      }
      return;
  }
  // Could be an unsupported register, or just a 32-bit one.
  ZX_ASSERT_MSG(false, "64-bit writes to 0x%lx are not supported", offs);
}

uint32_t FakeNvmeRegisters::Read32(zx_off_t offs) {
  switch (offs) {
    case nvme::NVME_REG_VS:
      return vers_.reg_value();
    case nvme::NVME_REG_INTMS:
    case nvme::NVME_REG_INTMC:
      ZX_ASSERT_MSG(false, "reads of interrupt mask are not supported");
    case nvme::NVME_REG_CC:
      return ccfg_.reg_value();
    case nvme::NVME_REG_CSTS:
      return csts_.reg_value();
    case nvme::NVME_REG_AQA:
      return admin_queue_attrs_.reg_value();
  }

  ZX_ASSERT_MSG(false, "32-bit reads from 0x%lx are not supported", offs);
}

void FakeNvmeRegisters::Write32(uint32_t val, zx_off_t offs) {
  if (offs >= nvme::NVME_REG_DOORBELL_BASE) {
    offs -= nvme::NVME_REG_DOORBELL_BASE;
    offs /= (4 << caps_.doorbell_stride());
    bool is_completion = offs & 1;
    offs /= 2;
    if (!is_completion) {
      ZX_ASSERT(offs < submission_doorbells_.size());
      submission_doorbells_[offs].set_reg_value(val);
      if (callbacks_) {
        callbacks_->doorbell_ring(true, offs, submission_doorbells_[offs]);
      }
    } else {
      ZX_ASSERT(offs < completion_doorbells_.size());
      completion_doorbells_[offs].set_reg_value(val);
      if (callbacks_) {
        callbacks_->doorbell_ring(false, offs, completion_doorbells_[offs]);
      }
    }
    return;
  }
  switch (offs) {
    case nvme::NVME_REG_VS:
      ZX_ASSERT_MSG(false, "VS register is read-only.");
    case nvme::NVME_REG_INTMS:
      interrupt_mask_set_.set_reg_value(val);
      if (callbacks_) {
        callbacks_->interrupt_mask_update(false, interrupt_mask_set_);
      }
      return;
    case nvme::NVME_REG_INTMC:
      interrupt_mask_clear_.set_reg_value(val);
      if (callbacks_) {
        callbacks_->interrupt_mask_update(true, interrupt_mask_clear_);
      }
      return;
    case nvme::NVME_REG_CC:
      ccfg_.set_reg_value(val);
      if (callbacks_) {
        callbacks_->set_config(ccfg_);
      }
      return;
    case nvme::NVME_REG_CSTS:
      ZX_ASSERT_MSG(false, "CSTS register is read-only.");
    case nvme::NVME_REG_AQA:
      admin_queue_attrs_.set_reg_value(val);
      return;
  }

  ZX_ASSERT_MSG(false, "32-bit reads from 0x%lx are not supported", offs);
}

}  // namespace fake_nvme
