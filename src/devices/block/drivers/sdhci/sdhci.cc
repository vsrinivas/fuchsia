// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes and limitations:
// 1. This driver only uses PIO mode.
//
// 2. This driver only supports SDHCv3 and above. Lower versions of SD are not
//    currently supported. The driver should fail gracefully if a lower version
//    card is detected.

#include "sdhci.h"

#include <inttypes.h>
#include <lib/zx/clock.h>
#include <lib/zx/pmt.h>
#include <lib/zx/time.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/block.h>
#include <fbl/alloc_checker.h>

#include "sdhci-reg.h"

namespace {

constexpr uint32_t kSdFreqSetupHz = 400'000;

constexpr int kMaxTuningCount = 40;

constexpr size_t kPageMask = PAGE_SIZE - 1;

constexpr uint32_t Hi32(zx_paddr_t val) { return static_cast<uint32_t>((val >> 32) & 0xffffffff); }
constexpr uint32_t Lo32(zx_paddr_t val) { return val & 0xffffffff; }

// for 2M max transfer size for fully discontiguous
// also see SDMMC_PAGES_COUNT in ddk/protocol/sdmmc.h
constexpr int kDmaDescCount = 512;

// 64k max per descriptor
static constexpr size_t kMaxDescriptorLength = 0x1'0000;  // 64k

constexpr zx::duration kResetTime = zx::sec(1);
constexpr zx::duration kClockStabilizationTime = zx::msec(150);
constexpr zx::duration kVoltageStabilizationTime = zx::msec(5);
constexpr zx::duration kInhibitWaitTime = zx::msec(1);
constexpr zx::duration kWaitYieldTime = zx::usec(1);

constexpr bool SdmmcCmdRspBusy(uint32_t cmd_flags) { return cmd_flags & SDMMC_RESP_LEN_48B; }

constexpr bool SdmmcCmdHasData(uint32_t cmd_flags) { return cmd_flags & SDMMC_RESP_DATA_PRESENT; }

uint16_t GetClockDividerValue(const uint32_t base_clock, const uint32_t target_rate) {
  if (target_rate >= base_clock) {
    // A clock divider of 0 means "don't divide the clock"
    // If the base clock is already slow enough to use as the SD clock then
    // we don't need to divide it any further.
    return 0;
  }

  uint32_t result = base_clock / (2 * target_rate);
  if (result * target_rate * 2 < base_clock)
    result++;

  return std::min(sdhci::ClockControl::kMaxFrequencySelect, static_cast<uint16_t>(result));
}

}  // namespace

namespace sdhci {

void Sdhci::PrepareCmd(sdmmc_req_t* req, TransferMode* transfer_mode, Command* command) {
  command->set_command_index(static_cast<uint16_t>(req->cmd_idx));

  if (req->cmd_flags & SDMMC_RESP_LEN_EMPTY) {
    command->set_response_type(Command::kResponseTypeNone);
  } else if (req->cmd_flags & SDMMC_RESP_LEN_136) {
    command->set_response_type(Command::kResponseType136Bits);
  } else if (req->cmd_flags & SDMMC_RESP_LEN_48) {
    command->set_response_type(Command::kResponseType48Bits);
  } else if (req->cmd_flags & SDMMC_RESP_LEN_48B) {
    command->set_response_type(Command::kResponseType48BitsWithBusy);
  }

  if (req->cmd_flags & SDMMC_CMD_TYPE_NORMAL) {
    command->set_command_type(Command::kCommandTypeNormal);
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_SUSPEND) {
    command->set_command_type(Command::kCommandTypeSuspend);
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_RESUME) {
    command->set_command_type(Command::kCommandTypeResume);
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_ABORT) {
    command->set_command_type(Command::kCommandTypeAbort);
  }

  if (req->cmd_flags & SDMMC_CMD_AUTO12) {
    transfer_mode->set_auto_cmd_enable(TransferMode::kAutoCmd12);
  } else if (req->cmd_flags & SDMMC_CMD_AUTO23) {
    transfer_mode->set_auto_cmd_enable(TransferMode::kAutoCmd23);
  }

  if (req->cmd_flags & SDMMC_RESP_CRC_CHECK) {
    command->set_command_crc_check(1);
  }
  if (req->cmd_flags & SDMMC_RESP_CMD_IDX_CHECK) {
    command->set_command_index_check(1);
  }
  if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    command->set_data_present(1);
  }
  if (req->cmd_flags & SDMMC_CMD_DMA_EN) {
    transfer_mode->set_dma_enable(1);
  }
  if (req->cmd_flags & SDMMC_CMD_BLKCNT_EN) {
    transfer_mode->set_block_count_enable(1);
  }
  if (req->cmd_flags & SDMMC_CMD_READ) {
    transfer_mode->set_read(1);
  }
  if (req->cmd_flags & SDMMC_CMD_MULTI_BLK) {
    transfer_mode->set_multi_block(1);
  }
}

zx_status_t Sdhci::WaitForReset(const SoftwareReset mask) {
  const zx::time deadline = zx::clock::get_monotonic() + kResetTime;
  do {
    if ((SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).reg_value() & mask.reg_value()) == 0) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(kWaitYieldTime));
  } while (zx::clock::get_monotonic() <= deadline);

  zxlogf(ERROR, "sdhci: timed out while waiting for reset\n");
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Sdhci::WaitForInhibit(const PresentState mask) const {
  const zx::time deadline = zx::clock::get_monotonic() + kInhibitWaitTime;
  do {
    if ((PresentState::Get().ReadFrom(&regs_mmio_buffer_).reg_value() & mask.reg_value()) == 0) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(kWaitYieldTime));
  } while (zx::clock::get_monotonic() <= deadline);

  zxlogf(ERROR, "sdhci: timed out while waiting for command/data inhibit\n");
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Sdhci::WaitForInternalClockStable() const {
  const zx::time deadline = zx::clock::get_monotonic() + kClockStabilizationTime;
  do {
    if ((ClockControl::Get().ReadFrom(&regs_mmio_buffer_).internal_clock_stable())) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(kWaitYieldTime));
  } while (zx::clock::get_monotonic() <= deadline);

  zxlogf(ERROR, "sdhci: timed out while waiting for internal clock to stabilize\n");
  return ZX_ERR_TIMED_OUT;
}

void Sdhci::CompleteRequestLocked(sdmmc_req_t* req, zx_status_t status) {
  zxlogf(TRACE, "sdhci: complete cmd 0x%08x status %d\n", req->cmd_idx, status);

  // Disable irqs when no pending transfer
  InterruptSignalEnable::Get().FromValue(0).WriteTo(&regs_mmio_buffer_);

  cmd_req_ = nullptr;
  data_req_ = nullptr;
  data_blockid_ = 0;
  data_done_ = false;

  req->status = status;
  sync_completion_signal(&req_completion_);
}

void Sdhci::CmdStageCompleteLocked() {
  zxlogf(TRACE, "sdhci: got CMD_CPLT interrupt\n");

  if (!cmd_req_) {
    zxlogf(TRACE, "sdhci: spurious CMD_CPLT interrupt!\n");
    return;
  }

  const uint32_t response_0 = Response::Get(0).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_1 = Response::Get(1).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_2 = Response::Get(2).ReadFrom(&regs_mmio_buffer_).reg_value();
  const uint32_t response_3 = Response::Get(3).ReadFrom(&regs_mmio_buffer_).reg_value();

  // Read the response data.
  if (cmd_req_->cmd_flags & SDMMC_RESP_LEN_136) {
    if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC) {
      cmd_req_->response[0] = (response_3 << 8) | ((response_2 >> 24) & 0xFF);
      cmd_req_->response[1] = (response_2 << 8) | ((response_1 >> 24) & 0xFF);
      cmd_req_->response[2] = (response_1 << 8) | ((response_0 >> 24) & 0xFF);
      cmd_req_->response[3] = (response_0 << 8);
    } else if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER) {
      cmd_req_->response[0] = (response_0 << 8);
      cmd_req_->response[1] = (response_1 << 8) | ((response_0 >> 24) & 0xFF);
      cmd_req_->response[2] = (response_2 << 8) | ((response_1 >> 24) & 0xFF);
      cmd_req_->response[3] = (response_3 << 8) | ((response_2 >> 24) & 0xFF);
    } else {
      cmd_req_->response[0] = response_0;
      cmd_req_->response[1] = response_1;
      cmd_req_->response[2] = response_2;
      cmd_req_->response[3] = response_3;
    }
  } else if (cmd_req_->cmd_flags & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
    cmd_req_->response[0] = response_0;
  }

  // We're done if the command has no data stage or if the data stage completed early
  if (!data_req_ || data_done_) {
    CompleteRequestLocked(cmd_req_, ZX_OK);
  } else {
    cmd_req_ = nullptr;
  }
}

void Sdhci::DataStageReadReadyLocked() {
  zxlogf(TRACE, "sdhci: got BUFF_READ_READY interrupt\n");

  if (!data_req_ || !SdmmcCmdHasData(data_req_->cmd_flags)) {
    zxlogf(TRACE, "sdhci: spurious BUFF_READ_READY interrupt!\n");
    return;
  }

  if ((data_req_->cmd_idx == MMC_SEND_TUNING_BLOCK) ||
      (data_req_->cmd_idx == SD_SEND_TUNING_BLOCK)) {
    // tuning command is done here
    CompleteRequestLocked(data_req_, ZX_OK);
  } else {
    // Sequentially read each block.
    uint32_t* const virt_buffer = reinterpret_cast<uint32_t*>(data_req_->virt_buffer) +
                                  ((data_blockid_ * data_req_->blocksize) / sizeof(uint32_t));
    for (size_t wordid = 0; wordid < (data_req_->blocksize / sizeof(uint32_t)); wordid++) {
      virt_buffer[wordid] = BufferData::Get().ReadFrom(&regs_mmio_buffer_).reg_value();
    }
    data_blockid_ = static_cast<uint16_t>(data_blockid_ + 1);
  }
}

void Sdhci::DataStageWriteReadyLocked() {
  zxlogf(TRACE, "sdhci: got BUFF_WRITE_READY interrupt\n");

  if (!data_req_ || !SdmmcCmdHasData(data_req_->cmd_flags)) {
    zxlogf(TRACE, "sdhci: spurious BUFF_WRITE_READY interrupt!\n");
    return;
  }

  // Sequentially write each block.
  const uint32_t* const virt_buffer = reinterpret_cast<uint32_t*>(data_req_->virt_buffer) +
                                      ((data_blockid_ * data_req_->blocksize) / sizeof(uint32_t));
  for (size_t wordid = 0; wordid < (data_req_->blocksize / sizeof(uint32_t)); wordid++) {
    BufferData::Get().FromValue(virt_buffer[wordid]).WriteTo(&regs_mmio_buffer_);
  }
  data_blockid_ = static_cast<uint16_t>(data_blockid_ + 1);
}

void Sdhci::TransferCompleteLocked() {
  zxlogf(TRACE, "sdhci: got XFER_CPLT interrupt\n");
  if (!data_req_) {
    zxlogf(TRACE, "sdhci: spurious XFER_CPLT interrupt!\n");
    return;
  }
  if (cmd_req_) {
    data_done_ = true;
  } else {
    CompleteRequestLocked(data_req_, ZX_OK);
  }
}

void Sdhci::ErrorRecoveryLocked() {
  // Reset internal state machines
  SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).set_reset_cmd(1).WriteTo(&regs_mmio_buffer_);
  WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_cmd(1));
  SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).set_reset_dat(1).WriteTo(&regs_mmio_buffer_);
  WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_dat(1));

  // Complete any pending txn with error status
  if (cmd_req_ != nullptr) {
    CompleteRequestLocked(cmd_req_, ZX_ERR_IO);
  } else if (data_req_ != nullptr) {
    CompleteRequestLocked(data_req_, ZX_ERR_IO);
  }
}

int Sdhci::IrqThread() {
  while (true) {
    zx_status_t wait_res = WaitForInterrupt();
    if (wait_res != ZX_OK) {
      if (wait_res != ZX_ERR_CANCELED) {
        zxlogf(ERROR, "sdhci: interrupt wait failed with retcode = %d\n", wait_res);
      }
      break;
    }

    // Acknowledge the IRQs that we stashed. IRQs are cleared by writing
    // 1s into the IRQs that fired.
    auto irq = InterruptStatus::Get().ReadFrom(&regs_mmio_buffer_).WriteTo(&regs_mmio_buffer_);

    zxlogf(TRACE, "got irq 0x%08x en 0x%08x\n", irq.reg_value(),
           InterruptSignalEnable::Get().ReadFrom(&regs_mmio_buffer_).reg_value());

    fbl::AutoLock lock(&mtx_);
    if (irq.command_complete()) {
      CmdStageCompleteLocked();
    }
    if (irq.buffer_read_ready()) {
      DataStageReadReadyLocked();
    }
    if (irq.buffer_write_ready()) {
      DataStageWriteReadyLocked();
    }
    if (irq.transfer_complete()) {
      TransferCompleteLocked();
    }
    if (irq.card_interrupt() && interrupt_cb_.is_valid()) {
      interrupt_cb_.Callback();
    }
    if (irq.ErrorInterrupt()) {
      if (driver_get_log_flags() & DDK_LOG_TRACE) {
        if (irq.adma_error()) {
          zxlogf(TRACE, "sdhci: ADMA error 0x%x ADMAADDR0 0x%x ADMAADDR1 0x%x\n",
                 AdmaErrorStatus::Get().ReadFrom(&regs_mmio_buffer_).reg_value(),
                 AdmaSystemAddress::Get(0).ReadFrom(&regs_mmio_buffer_).reg_value(),
                 AdmaSystemAddress::Get(1).ReadFrom(&regs_mmio_buffer_).reg_value());
        }
      }
      ErrorRecoveryLocked();
    }
  }
  return thrd_success;
}

template <typename DescriptorType>
zx_status_t Sdhci::BuildDmaDescriptor(sdmmc_req_t* req, DescriptorType* descs) {
  constexpr zx_paddr_t kPhysAddrMask =
      sizeof(descs->address) == sizeof(uint32_t) ? 0x0000'0000'ffff'ffff : 0xffff'ffff'ffff'ffff;

  const uint64_t req_len = req->blockcount * req->blocksize;
  const bool is_read = req->cmd_flags & SDMMC_CMD_READ;

  const uint64_t pagecount = ((req->buf_offset & kPageMask) + req_len + kPageMask) / PAGE_SIZE;
  if (pagecount > SDMMC_PAGES_COUNT) {
    zxlogf(ERROR, "sdhci: too many pages %lu vs %lu\n", pagecount, SDMMC_PAGES_COUNT);
    return ZX_ERR_INVALID_ARGS;
  }

  // pin the vmo
  zx::unowned_vmo dma_vmo(req->dma_vmo);
  zx_paddr_t phys[SDMMC_PAGES_COUNT];
  zx::pmt pmt;
  // offset_vmo is converted to bytes by the sdmmc layer
  const uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
  zx_status_t st = bti_.pin(options, *dma_vmo, req->buf_offset & ~kPageMask, pagecount * PAGE_SIZE,
                            phys, pagecount, &pmt);
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdhci: error %d bti_pin\n", st);
    return st;
  }
  if (req->cmd_flags & SDMMC_CMD_READ) {
    st = dma_vmo->op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset, req_len, nullptr, 0);
  } else {
    st = dma_vmo->op_range(ZX_VMO_OP_CACHE_CLEAN, req->buf_offset, req_len, nullptr, 0);
  }
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdhci: cache clean failed with error  %d\n", st);
    return st;
  }
  // cache this for zx_pmt_unpin() later
  req->pmt = pmt.release();

  phys_iter_buffer_t buf = {
      .phys = phys,
      .phys_count = pagecount,
      .length = req_len,
      .vmo_offset = req->buf_offset,
      .sg_list = nullptr,
      .sg_count = 0,
  };
  phys_iter_t iter;
  phys_iter_init(&iter, &buf, kMaxDescriptorLength);

  int count = 0;
  for (DescriptorType* desc = descs;; desc++) {
    zx_paddr_t paddr;
    size_t length = phys_iter_next(&iter, &paddr);
    if (length == 0) {
      if (desc != descs) {
        desc -= 1;
        // set end bit on the last descriptor
        desc->attr = Adma2DescriptorAttributes::Get(desc->attr).set_end(1).reg_value();
        break;
      } else {
        zxlogf(TRACE, "sdhci: empty descriptor list!\n");
        return ZX_ERR_NOT_SUPPORTED;
      }
    } else if (length > kMaxDescriptorLength) {
      zxlogf(TRACE, "sdhci: chunk size > %zu is unsupported\n", length);
      return ZX_ERR_NOT_SUPPORTED;
    } else if ((++count) > kDmaDescCount) {
      zxlogf(TRACE, "sdhci: request with more than %zd chunks is unsupported\n", length);
      return ZX_ERR_NOT_SUPPORTED;
    }

    if ((paddr & kPhysAddrMask) != paddr) {
      zxlogf(ERROR, "sdhci: 64-bit physical address supplied for 32-bit DMA\n");
      return ZX_ERR_NOT_SUPPORTED;
    }

    if constexpr (sizeof(desc->address) == sizeof(uint32_t)) {
      desc->address = static_cast<uint32_t>(paddr);
    } else {
      desc->address = paddr;
    }
    desc->length = static_cast<uint16_t>(length);
    desc->attr = Adma2DescriptorAttributes::Get()
                     .set_valid(1)
                     .set_type(Adma2DescriptorAttributes::kTypeData)
                     .reg_value();
  }

  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    DescriptorType* desc = descs;
    do {
      if constexpr (sizeof(desc->address) == sizeof(uint32_t)) {
        zxlogf(SPEW, "desc: addr=0x%" PRIx32 " length=0x%04x attr=0x%04x\n", desc->address,
               desc->length, desc->attr);
      } else {
        zxlogf(SPEW, "desc: addr=0x%" PRIx64 " length=0x%04x attr=0x%04x\n", desc->address,
               desc->length, desc->attr);
      }
    } while (!Adma2DescriptorAttributes::Get((desc++)->attr).end());
  }

  zx_paddr_t desc_phys = iobuf_.phys();
  if ((desc_phys & kPhysAddrMask) != desc_phys) {
    zxlogf(ERROR, "sdhci: 64-bit physical address supplied for 32-bit DMA\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  AdmaSystemAddress::Get(0).FromValue(Lo32(desc_phys)).WriteTo(&regs_mmio_buffer_);
  AdmaSystemAddress::Get(1).FromValue(Hi32(desc_phys)).WriteTo(&regs_mmio_buffer_);

  zxlogf(SPEW, "sdhci: descs at 0x%x 0x%x\n", Lo32(desc_phys), Hi32(desc_phys));

  return ZX_OK;
}

zx_status_t Sdhci::StartRequestLocked(sdmmc_req_t* req) {
  const uint32_t arg = req->arg;
  const uint16_t blkcnt = req->blockcount;
  const uint16_t blksiz = req->blocksize;
  const bool has_data = SdmmcCmdHasData(req->cmd_flags);

  Command command = Command::Get().FromValue(0);
  TransferMode transfer_mode = TransferMode::Get().FromValue(0);
  PrepareCmd(req, &transfer_mode, &command);

  if (req->use_dma && !SupportsAdma2()) {
    zxlogf(TRACE, "sdhci: host does not support DMA\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zxlogf(TRACE, "sdhci: start_req cmd=0x%08x (data %d dma %d bsy %d) blkcnt %u blksiz %u\n",
         command.reg_value(), has_data, req->use_dma, SdmmcCmdRspBusy(req->cmd_flags), blkcnt,
         blksiz);

  // Every command requires that the Command Inhibit is unset.
  auto inhibit_mask = PresentState::Get().FromValue(0).set_command_inhibit_cmd(1);

  // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
  // it's an abort command which can be issued with the data lines active.
  if ((req->cmd_flags & SDMMC_RESP_LEN_48B) && (req->cmd_flags & SDMMC_CMD_TYPE_ABORT)) {
    inhibit_mask.set_command_inhibit_dat(1);
  }

  // Wait for the inhibit masks from above to become 0 before issuing the command.
  zx_status_t st = WaitForInhibit(inhibit_mask);
  if (st != ZX_OK) {
    return st;
  }

  if (has_data) {
    if (req->use_dma) {
      if (Capabilities0::Get().ReadFrom(&regs_mmio_buffer_).v3_64_bit_system_address_support()) {
        st = BuildDmaDescriptor(req, reinterpret_cast<AdmaDescriptor96*>(iobuf_.virt()));
      } else {
        st = BuildDmaDescriptor(req, reinterpret_cast<AdmaDescriptor64*>(iobuf_.virt()));
      }

      if (st != ZX_OK) {
        zxlogf(ERROR, "sdhci: failed to build DMA descriptor\n");
        return st;
      }
      transfer_mode.set_dma_enable(1);
    }

    if (req->cmd_flags & SDMMC_CMD_MULTI_BLK) {
      transfer_mode.set_auto_cmd_enable(TransferMode::kAutoCmd12);
    }
  }

  BlockSize::Get().FromValue(blksiz).WriteTo(&regs_mmio_buffer_);
  BlockCount::Get().FromValue(blkcnt).WriteTo(&regs_mmio_buffer_);

  Argument::Get().FromValue(arg).WriteTo(&regs_mmio_buffer_);

  // Clear any pending interrupts before starting the transaction.
  auto irq_mask = InterruptSignalEnable::Get().ReadFrom(&regs_mmio_buffer_);
  InterruptStatus::Get().FromValue(irq_mask.reg_value()).WriteTo(&regs_mmio_buffer_);

  // Unmask and enable interrupts
  irq_mask.set_reg_value(0).EnableErrorInterrupts().EnableNormalInterrupts().WriteTo(
      &regs_mmio_buffer_);
  InterruptStatusEnable::Get()
      .FromValue(0)
      .EnableErrorInterrupts()
      .EnableNormalInterrupts()
      .WriteTo(&regs_mmio_buffer_);

  // Start command
  transfer_mode.WriteTo(&regs_mmio_buffer_);
  command.WriteTo(&regs_mmio_buffer_);

  cmd_req_ = req;
  if (has_data || SdmmcCmdRspBusy(req->cmd_flags)) {
    data_req_ = req;
  } else {
    data_req_ = nullptr;
  }
  data_blockid_ = 0;
  data_done_ = false;
  return ZX_OK;
}

zx_status_t Sdhci::FinishRequest(sdmmc_req_t* req) {
  if (req->use_dma && req->pmt != ZX_HANDLE_INVALID) {
    // Clean the cache one more time after the DMA operation because there
    // might be a possibility of cpu prefetching while the DMA operation is
    // going on.
    zx_status_t st;
    const uint64_t req_len = req->blockcount * req->blocksize;
    if ((req->cmd_flags & SDMMC_CMD_READ) && req->use_dma) {
      st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset, req_len,
                           nullptr, 0);
      if (st != ZX_OK) {
        zxlogf(ERROR, "sdhci: cache clean failed with error  %d\n", st);
        return st;
      }
    }
    st = zx_pmt_unpin(req->pmt);
    req->pmt = ZX_HANDLE_INVALID;
    if (st != ZX_OK) {
      zxlogf(ERROR, "sdhci: error %d in pmt_unpin\n", st);
      return st;
    }
  }

  if (req->cmd_flags & SDMMC_CMD_TYPE_ABORT) {
    // SDHCI spec section 3.8.2: reset the data line after an abort to discard data in the buffer.
    return WaitForReset(SoftwareReset::Get().FromValue(0).set_reset_cmd(1).set_reset_dat(1));
  }
  return ZX_OK;
}

zx_status_t Sdhci::SdmmcHostInfo(sdmmc_host_info_t* out_info) {
  memcpy(out_info, &info_, sizeof(info_));
  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
  fbl::AutoLock lock(&mtx_);

  // Validate the controller supports the requested voltage
  if ((voltage == SDMMC_VOLTAGE_V330) && !(info_.caps & SDMMC_HOST_CAP_VOLTAGE_330)) {
    zxlogf(TRACE, "sdhci: 3.3V signal voltage not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto ctrl2 = HostControl2::Get().ReadFrom(&regs_mmio_buffer_);
  uint16_t voltage_1v8_value = 0;
  switch (voltage) {
    case SDMMC_VOLTAGE_V180: {
      voltage_1v8_value = 1;
      break;
    }
    case SDMMC_VOLTAGE_V330: {
      voltage_1v8_value = 0;
      break;
    }
    default:
      zxlogf(ERROR, "sdhci: unknown signal voltage value %u\n", voltage);
      return ZX_ERR_INVALID_ARGS;
  }

  // Note: the SDHCI spec indicates that the data lines should be checked to see if the card is
  // ready for a voltage switch, however that doesn't seem to work for one of our devices.

  ctrl2.set_voltage_1v8_signalling_enable(voltage_1v8_value).WriteTo(&regs_mmio_buffer_);

  // Wait 5ms for the regulator to stabilize.
  zx::nanosleep(zx::deadline_after(kVoltageStabilizationTime));

  if (ctrl2.ReadFrom(&regs_mmio_buffer_).voltage_1v8_signalling_enable() != voltage_1v8_value) {
    zxlogf(ERROR, "sdhci: voltage regulator output did not become stable\n");
    // Cut power to the card if the voltage switch failed.
    PowerControl::Get()
        .ReadFrom(&regs_mmio_buffer_)
        .set_sd_bus_power_vdd1(0)
        .WriteTo(&regs_mmio_buffer_);
    return ZX_ERR_INTERNAL;
  }

  zxlogf(TRACE, "sdhci: switch signal voltage to %d\n", voltage);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
  fbl::AutoLock lock(&mtx_);

  if ((bus_width == SDMMC_BUS_WIDTH_EIGHT) && !(info_.caps & SDMMC_HOST_CAP_BUS_WIDTH_8)) {
    zxlogf(TRACE, "sdhci: 8-bit bus width not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if ((quirks_ & SDHCI_QUIRK_BUS_WIDTH_1) && (bus_width != SDMMC_BUS_WIDTH_ONE)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto ctrl1 = HostControl1::Get().ReadFrom(&regs_mmio_buffer_);

  switch (bus_width) {
    case SDMMC_BUS_WIDTH_ONE:
      ctrl1.set_extended_data_transfer_width(0).set_data_transfer_width_4bit(0);
      break;
    case SDMMC_BUS_WIDTH_FOUR:
      ctrl1.set_extended_data_transfer_width(0).set_data_transfer_width_4bit(1);
      break;
    case SDMMC_BUS_WIDTH_EIGHT:
      ctrl1.set_extended_data_transfer_width(1).set_data_transfer_width_4bit(0);
      break;
    default:
      zxlogf(ERROR, "sdhci: unknown bus width value %u\n", bus_width);
      return ZX_ERR_INVALID_ARGS;
  }

  ctrl1.WriteTo(&regs_mmio_buffer_);

  zxlogf(TRACE, "sdhci: set bus width to %d\n", bus_width);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetBusFreq(uint32_t bus_freq) {
  fbl::AutoLock lock(&mtx_);

  zx_status_t st = WaitForInhibit(
      PresentState::Get().FromValue(0).set_command_inhibit_cmd(1).set_command_inhibit_dat(1));
  if (st != ZX_OK) {
    return st;
  }

  // Turn off the SD clock before messing with the clock rate.
  auto clock = ClockControl::Get().ReadFrom(&regs_mmio_buffer_).set_sd_clock_enable(0);
  if (bus_freq == 0) {
    clock.WriteTo(&regs_mmio_buffer_);
    return ZX_OK;
  }
  clock.set_internal_clock_enable(0).WriteTo(&regs_mmio_buffer_);

  // Write the new divider into the control register.
  clock.set_frequency_select(GetClockDividerValue(base_clock_, bus_freq))
      .set_internal_clock_enable(1)
      .WriteTo(&regs_mmio_buffer_);

  if ((st = WaitForInternalClockStable()) != ZX_OK) {
    return st;
  }

  // Turn the SD clock back on.
  clock.set_sd_clock_enable(1).WriteTo(&regs_mmio_buffer_);

  zxlogf(TRACE, "sdhci: set bus frequency to %u\n", bus_freq);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetTiming(sdmmc_timing_t timing) {
  fbl::AutoLock lock(&mtx_);

  auto ctrl1 = HostControl1::Get().ReadFrom(&regs_mmio_buffer_);

  // Toggle high-speed
  if (timing != SDMMC_TIMING_LEGACY) {
    ctrl1.set_high_speed_enable(1).WriteTo(&regs_mmio_buffer_);
  } else {
    ctrl1.set_high_speed_enable(0).WriteTo(&regs_mmio_buffer_);
  }

  auto ctrl2 = HostControl2::Get().ReadFrom(&regs_mmio_buffer_);
  switch (timing) {
    case SDMMC_TIMING_LEGACY:
    case SDMMC_TIMING_SDR12:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeSdr12);
      break;
    case SDMMC_TIMING_HS:
    case SDMMC_TIMING_SDR25:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeSdr25);
      break;
    case SDMMC_TIMING_HSDDR:
    case SDMMC_TIMING_DDR50:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeDdr50);
      break;
    case SDMMC_TIMING_HS200:
    case SDMMC_TIMING_SDR104:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeSdr104);
      break;
    case SDMMC_TIMING_HS400:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeHs400);
      break;
    case SDMMC_TIMING_SDR50:
      ctrl2.set_uhs_mode_select(HostControl2::kUhsModeSdr50);
      break;
    default:
      zxlogf(ERROR, "sdhci: unknown timing value %u\n", timing);
      return ZX_ERR_INVALID_ARGS;
  }
  ctrl2.WriteTo(&regs_mmio_buffer_);

  zxlogf(TRACE, "sdhci: set bus timing to %d\n", timing);

  return ZX_OK;
}

void Sdhci::SdmmcHwReset() {
  fbl::AutoLock lock(&mtx_);
  sdhci_.HwReset();
}

zx_status_t Sdhci::SdmmcRequest(sdmmc_req_t* req) {
  zx_status_t st = ZX_OK;

  {
    fbl::AutoLock lock(&mtx_);

    // one command at a time
    if ((cmd_req_ != nullptr) || (data_req_ != nullptr)) {
      st = ZX_ERR_SHOULD_WAIT;
    } else {
      st = StartRequestLocked(req);
    }
  }

  if (st != ZX_OK) {
    FinishRequest(req);
    return st;
  }

  sync_completion_wait(&req_completion_, ZX_TIME_INFINITE);

  FinishRequest(req);

  sync_completion_reset(&req_completion_);

  return req->status;
}

zx_status_t Sdhci::SdmmcPerformTuning(uint32_t cmd_idx) {
  zxlogf(TRACE, "sdhci: perform tuning\n");

  uint16_t blocksize;
  auto ctrl2 = HostControl2::Get().FromValue(0);

  {
    fbl::AutoLock lock(&mtx_);
    blocksize = static_cast<uint16_t>(
        HostControl1::Get().ReadFrom(&regs_mmio_buffer_).extended_data_transfer_width() ? 128 : 64);
    ctrl2.ReadFrom(&regs_mmio_buffer_).set_execute_tuning(1).WriteTo(&regs_mmio_buffer_);
  }

  sdmmc_req_t req = {
      .cmd_idx = cmd_idx,
      .cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS,
      .arg = 0,
      .blockcount = 0,
      .blocksize = blocksize,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = nullptr,
      .virt_size = 0,
      .buf_offset = 0,
      .pmt = ZX_HANDLE_INVALID,
      .probe_tuning_cmd = true,
      .response = {},
      .status = ZX_ERR_BAD_STATE,
  };

  for (int count = 0; (count < kMaxTuningCount) && ctrl2.execute_tuning(); count++) {
    zx_status_t st = SdmmcRequest(&req);
    if (st != ZX_OK) {
      zxlogf(ERROR, "sdhci: MMC_SEND_TUNING_BLOCK error, retcode = %d\n", req.status);
      return st;
    }

    fbl::AutoLock lock(&mtx_);
    ctrl2.ReadFrom(&regs_mmio_buffer_);
  }

  {
    fbl::AutoLock lock(&mtx_);
    ctrl2.ReadFrom(&regs_mmio_buffer_);
  }

  const bool fail = ctrl2.execute_tuning() || !ctrl2.use_tuned_clock();

  zxlogf(TRACE, "sdhci: tuning fail %d\n", fail);

  return fail ? ZX_ERR_IO : ZX_OK;
}

zx_status_t Sdhci::SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb) {
  interrupt_cb_ = ddk::InBandInterruptProtocolClient(interrupt_cb);
  return ZX_OK;
}

zx_status_t Sdhci::SdmmcRegisterVmo(uint32_t vmo_id, zx::vmo vmo, uint64_t offset, uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sdhci::SdmmcUnregisterVmo(uint32_t vmo_id, zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sdhci::SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) {
  return ZX_ERR_NOT_SUPPORTED;
}

void Sdhci::DdkUnbindNew(ddk::UnbindTxn txn) {
  // stop irq thread
  irq_.destroy();
  thrd_join(irq_thread_, nullptr);

  txn.Reply();
}

void Sdhci::DdkRelease() { delete this; }

zx_status_t Sdhci::Init() {
  // Ensure that we're SDv3.
  const uint16_t vrsn =
      HostControllerVersion::Get().ReadFrom(&regs_mmio_buffer_).specification_version();
  if (vrsn < HostControllerVersion::kSpecificationVersion300) {
    zxlogf(ERROR, "sdhci: SD version is %u, only version %u is supported\n", vrsn,
           HostControllerVersion::kSpecificationVersion300);
    return ZX_ERR_NOT_SUPPORTED;
  }
  zxlogf(TRACE, "sdhci: controller version %d\n", vrsn);

  auto caps0 = Capabilities0::Get().ReadFrom(&regs_mmio_buffer_);
  auto caps1 = Capabilities1::Get().ReadFrom(&regs_mmio_buffer_);

  base_clock_ = caps0.base_clock_frequency_hz();
  if (base_clock_ == 0) {
    // try to get controller specific base clock
    base_clock_ = sdhci_.GetBaseClock();
  }
  if (base_clock_ == 0) {
    zxlogf(ERROR, "sdhci: base clock is 0!\n");
    return ZX_ERR_INTERNAL;
  }

  // Get controller capabilities
  if (caps0.bus_width_8_support()) {
    info_.caps |= SDMMC_HOST_CAP_BUS_WIDTH_8;
  }
  if (caps0.adma2_support() && !(quirks_ & SDHCI_QUIRK_NO_DMA)) {
    info_.caps |= SDMMC_HOST_CAP_DMA;
  }
  if (caps0.voltage_3v3_support()) {
    info_.caps |= SDMMC_HOST_CAP_VOLTAGE_330;
  }
  if (caps1.sdr50_support()) {
    info_.caps |= SDMMC_HOST_CAP_SDR50;
  }
  if (caps1.ddr50_support()) {
    info_.caps |= SDMMC_HOST_CAP_DDR50;
  }
  if (caps1.sdr104_support()) {
    info_.caps |= SDMMC_HOST_CAP_SDR104;
  }
  if (!caps1.use_tuning_for_sdr50()) {
    info_.caps |= SDMMC_HOST_CAP_NO_TUNING_SDR50;
  }
  info_.caps |= SDMMC_HOST_CAP_AUTO_CMD12;

  // Set controller preferences
  if (quirks_ & SDHCI_QUIRK_NON_STANDARD_TUNING) {
    // Disable HS200 and HS400 if tuning cannot be performed as per the spec.
    info_.prefs |= SDMMC_HOST_PREFS_DISABLE_HS200 | SDMMC_HOST_PREFS_DISABLE_HS400;
  }

  // Perform a software reset against both the DAT and CMD interface.
  SoftwareReset::Get().ReadFrom(&regs_mmio_buffer_).set_reset_all(1).WriteTo(&regs_mmio_buffer_);

  // Disable both clocks.
  auto clock = ClockControl::Get().ReadFrom(&regs_mmio_buffer_);
  clock.set_internal_clock_enable(0).set_sd_clock_enable(0).WriteTo(&regs_mmio_buffer_);

  // Wait for reset to take place. The reset is completed when all three
  // of the following flags are reset.
  const SoftwareReset target_mask =
      SoftwareReset::Get().FromValue(0).set_reset_all(1).set_reset_cmd(1).set_reset_dat(1);
  zx_status_t status = ZX_OK;
  if ((status = WaitForReset(target_mask)) != ZX_OK) {
    return status;
  }

  // allocate and setup DMA descriptor
  if (SupportsAdma2()) {
    auto host_control1 = HostControl1::Get().ReadFrom(&regs_mmio_buffer_);
    if (caps0.v3_64_bit_system_address_support()) {
      status = iobuf_.Init(bti_.get(), kDmaDescCount * sizeof(AdmaDescriptor96),
                           IO_BUFFER_RW | IO_BUFFER_CONTIG);
      host_control1.set_dma_select(HostControl1::kDmaSelect64BitAdma2);
    } else {
      status = iobuf_.Init(bti_.get(), kDmaDescCount * sizeof(AdmaDescriptor64),
                           IO_BUFFER_RW | IO_BUFFER_CONTIG);
      host_control1.set_dma_select(HostControl1::kDmaSelect32BitAdma2);
    }

    if (status != ZX_OK) {
      zxlogf(ERROR, "sdhci: error allocating DMA descriptors\n");
      return status;
    }
    info_.max_transfer_size = kDmaDescCount * PAGE_SIZE;

    host_control1.WriteTo(&regs_mmio_buffer_);
  } else {
    // no maximum if only PIO supported
    info_.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
  }
  info_.max_transfer_size_non_dma = BLOCK_MAX_TRANSFER_UNBOUNDED;

  // Configure the clock.
  clock.ReadFrom(&regs_mmio_buffer_).set_internal_clock_enable(1);

  // SDHCI Versions 1.00 and 2.00 handle the clock divider slightly
  // differently compared to SDHCI version 3.00. Since this driver doesn't
  // support SDHCI versions < 3.00, we ignore this incongruency for now.
  //
  // V3.00 supports a 10 bit divider where the SD clock frequency is defined
  // as F/(2*D) where F is the base clock frequency and D is the divider.
  clock.set_frequency_select(GetClockDividerValue(base_clock_, kSdFreqSetupHz))
      .WriteTo(&regs_mmio_buffer_);

  // Wait for the clock to stabilize.
  status = WaitForInternalClockStable();
  if (status != ZX_OK) {
    return ZX_ERR_TIMED_OUT;
  }

  // Set the command timeout.
  TimeoutControl::Get()
      .ReadFrom(&regs_mmio_buffer_)
      .set_data_timeout_counter(TimeoutControl::kDataTimeoutMax)
      .WriteTo(&regs_mmio_buffer_);

  // Set SD bus voltage to maximum supported by the host controller
  auto power = PowerControl::Get().ReadFrom(&regs_mmio_buffer_).set_sd_bus_power_vdd1(1);
  if (info_.caps & SDMMC_HOST_CAP_VOLTAGE_330) {
    power.set_sd_bus_voltage_vdd1(PowerControl::kBusVoltage3V3);
  } else {
    power.set_sd_bus_voltage_vdd1(PowerControl::kBusVoltage1V8);
  }
  power.WriteTo(&regs_mmio_buffer_);

  // Enable the SD clock.
  clock.ReadFrom(&regs_mmio_buffer_).set_sd_clock_enable(1).WriteTo(&regs_mmio_buffer_);

  // Disable all interrupts
  InterruptStatus::Get().FromValue(0).ClearAll().WriteTo(&regs_mmio_buffer_);
  InterruptSignalEnable::Get().FromValue(0).MaskAll().WriteTo(&regs_mmio_buffer_);

  if (thrd_create_with_name(
          &irq_thread_, [](void* arg) -> int { return reinterpret_cast<Sdhci*>(arg)->IrqThread(); },
          this, "sdhci_irq_thread") != thrd_success) {
    zxlogf(ERROR, "sdhci: failed to create irq thread\n");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t Sdhci::Create(void* ctx, zx_device_t* parent) {
  ddk::SdhciProtocolClient sdhci(parent);
  if (!sdhci.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Map the Device Registers so that we can perform MMIO against the device.
  zx::vmo vmo;
  zx_off_t vmo_offset = 0;
  zx_status_t status = sdhci.GetMmio(&vmo, &vmo_offset);
  if (status != ZX_OK) {
    zxlogf(ERROR, "sdhci: error %d in get_mmio\n", status);
    return status;
  }
  std::optional<ddk::MmioBuffer> regs_mmio_buffer;
  status = ddk::MmioBuffer::Create(vmo_offset, kRegisterSetSize, std::move(vmo),
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &regs_mmio_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "sdhci: error %d in mmio_buffer_init\n", status);
    return status;
  }
  zx::bti bti;
  status = sdhci.GetBti(0, &bti);
  if (status != ZX_OK) {
    zxlogf(ERROR, "sdhci: error %d in get_bti\n", status);
    return status;
  }

  zx::interrupt irq;
  status = sdhci.GetInterrupt(&irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "sdhci: error %d in get_interrupt\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<Sdhci>(&ac, parent, *std::move(regs_mmio_buffer),
                                             std::move(bti), std::move(irq), sdhci);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // initialize the controller
  status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SDHCI Controller init failed\n", __func__);
    return status;
  }

  status = dev->DdkAdd("sdhci");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SDMMC device_add failed.\n", __func__);
    dev->irq_.destroy();
    thrd_join(dev->irq_thread_, nullptr);
    return status;
  }

  __UNUSED auto _ = dev.release();
  return ZX_OK;
}

}  // namespace sdhci

static constexpr zx_driver_ops_t sdhci_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdhci::Sdhci::Create;
  return ops;
}();

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(sdhci, sdhci_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SDHCI)
ZIRCON_DRIVER_END(sdhci)
// clang-format on
