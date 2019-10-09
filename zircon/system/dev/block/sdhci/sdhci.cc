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

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/block.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/zx/clock.h>
#include <lib/zx/pmt.h>
#include <lib/zx/time.h>

namespace {

constexpr uint32_t kMhzToHz = 1'000'000;
constexpr uint32_t kSdFreqSetupHz = 400'000;

constexpr int kMaxTuningCount = 40;

constexpr size_t kPageMask = PAGE_SIZE - 1;

constexpr uint32_t Hi32(zx_paddr_t val) { return static_cast<uint32_t>((val >> 32) & 0xffffffff); }
constexpr uint32_t Lo32(zx_paddr_t val) { return val & 0xffffffff; }

// for 2M max transfer size for fully discontiguous
// also see SDMMC_PAGES_COUNT in ddk/protocol/sdmmc.h
constexpr int kDmaDescCount = 512;

// If any of these interrupts is asserted in the SDHCI irq register, it means
// that an error has occurred.
constexpr uint32_t kErrorInterrupts =
    (SDHCI_IRQ_ERR | SDHCI_IRQ_ERR_CMD_TIMEOUT | SDHCI_IRQ_ERR_CMD_CRC | SDHCI_IRQ_ERR_CMD_END_BIT |
     SDHCI_IRQ_ERR_CMD_INDEX | SDHCI_IRQ_ERR_DAT_TIMEOUT | SDHCI_IRQ_ERR_DAT_CRC |
     SDHCI_IRQ_ERR_DAT_ENDBIT | SDHCI_IRQ_ERR_CURRENT_LIMIT | SDHCI_IRQ_ERR_AUTO_CMD |
     SDHCI_IRQ_ERR_ADMA | SDHCI_IRQ_ERR_TUNING);

// These interrupts indicate that a transfer or command has progressed normally.
constexpr uint32_t kNormalInterrupts = (SDHCI_IRQ_CMD_CPLT | SDHCI_IRQ_XFER_CPLT |
                                        SDHCI_IRQ_BUFF_READ_READY | SDHCI_IRQ_BUFF_WRITE_READY);

constexpr zx::duration kResetTime                = zx::sec(1);
constexpr zx::duration kClockStabilizationTime   = zx::sec(1);
constexpr zx::duration kVoltageStabilizationTime = zx::msec(5);
constexpr zx::duration kControlUpdateWaitTime    = zx::msec(2);
constexpr zx::duration kInhibitWaitTime          = zx::msec(1);

constexpr bool SdmmcCmdRspBusy(uint32_t cmd_flags) { return cmd_flags & SDMMC_RESP_LEN_48B; }

constexpr bool SdmmcCmdHasData(uint32_t cmd_flags) { return cmd_flags & SDMMC_RESP_DATA_PRESENT; }

uint32_t GetClockDividerValue(const uint32_t base_clock, const uint32_t target_rate) {
  if (target_rate >= base_clock) {
    // A clock divider of 0 means "don't divide the clock"
    // If the base clock is already slow enough to use as the SD clock then
    // we don't need to divide it any further.
    return 0;
  }

  uint32_t result = base_clock / (2 * target_rate);
  if (result * target_rate * 2 < base_clock)
    result++;

  return (((result >> 8) & 0x3) | (result << 2)) << SDHCI_SD_CLOCK_FREQUENCY_SELECT_SHIFT;
}

}  // namespace

namespace sdhci {

uint32_t Sdhci::PrepareCmd(sdmmc_req_t* req) {
  constexpr uint32_t kSdmmcSdhciMap[][2] = {
      {SDMMC_RESP_CRC_CHECK, SDHCI_CMD_RESP_CRC_CHECK},
      {SDMMC_RESP_CMD_IDX_CHECK, SDHCI_CMD_RESP_CMD_IDX_CHECK},
      {SDMMC_RESP_DATA_PRESENT, SDHCI_CMD_RESP_DATA_PRESENT},
      {SDMMC_CMD_DMA_EN, SDHCI_CMD_DMA_EN},
      {SDMMC_CMD_BLKCNT_EN, SDHCI_CMD_BLKCNT_EN},
      {SDMMC_CMD_AUTO12, SDHCI_CMD_AUTO12},
      {SDMMC_CMD_AUTO23, SDHCI_CMD_AUTO23},
      {SDMMC_CMD_READ, SDHCI_CMD_READ},
      {SDMMC_CMD_MULTI_BLK, SDHCI_CMD_MULTI_BLK},
  };

  uint32_t cmd = SDHCI_CMD_IDX(req->cmd_idx);

  if (req->cmd_flags & SDMMC_RESP_LEN_EMPTY) {
    cmd |= SDHCI_CMD_RESP_LEN_EMPTY;
  } else if (req->cmd_flags & SDMMC_RESP_LEN_136) {
    cmd |= SDHCI_CMD_RESP_LEN_136;
  } else if (req->cmd_flags & SDMMC_RESP_LEN_48) {
    cmd |= SDHCI_CMD_RESP_LEN_48;
  } else if (req->cmd_flags & SDMMC_RESP_LEN_48B) {
    cmd |= SDHCI_CMD_RESP_LEN_48B;
  }

  if (req->cmd_flags & SDMMC_CMD_TYPE_NORMAL) {
    cmd |= SDHCI_CMD_TYPE_NORMAL;
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_SUSPEND) {
    cmd |= SDHCI_CMD_TYPE_SUSPEND;
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_RESUME) {
    cmd |= SDHCI_CMD_TYPE_RESUME;
  } else if (req->cmd_flags & SDMMC_CMD_TYPE_ABORT) {
    cmd |= SDHCI_CMD_TYPE_ABORT;
  }

  for (unsigned i = 0; i < fbl::count_of(kSdmmcSdhciMap); i++) {
    if (req->cmd_flags & kSdmmcSdhciMap[i][0]) {
      cmd |= kSdmmcSdhciMap[i][1];
    }
  }
  return cmd;
}

zx_status_t Sdhci::WaitForReset(const uint32_t mask, zx::duration timeout) const {
  zx::time deadline = zx::clock::get_monotonic() + timeout;
  while (true) {
    if (((regs_->ctrl1) & mask) == 0) {
      break;
    }
    if (zx::clock::get_monotonic() > deadline) {
      zxlogf(ERROR, "sdhci: timed out while waiting for reset\n");
      return ZX_ERR_TIMED_OUT;
    }
  }
  return ZX_OK;
}

void Sdhci::CompleteRequestLocked(sdmmc_req_t* req, zx_status_t status) {
  zxlogf(TRACE, "sdhci: complete cmd 0x%08x status %d\n", req->cmd_idx, status);

  // Disable irqs when no pending transfer
  regs_->irqen = 0;

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

  const uint32_t cmd = PrepareCmd(cmd_req_);

  // Read the response data.
  if (cmd & SDHCI_CMD_RESP_LEN_136) {
    if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC) {
      cmd_req_->response[0] = (regs_->resp3 << 8) | ((regs_->resp2 >> 24) & 0xFF);
      cmd_req_->response[1] = (regs_->resp2 << 8) | ((regs_->resp1 >> 24) & 0xFF);
      cmd_req_->response[2] = (regs_->resp1 << 8) | ((regs_->resp0 >> 24) & 0xFF);
      cmd_req_->response[3] = (regs_->resp0 << 8);
    } else if (quirks_ & SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER) {
      cmd_req_->response[0] = (regs_->resp0 << 8);
      cmd_req_->response[1] = (regs_->resp1 << 8) | ((regs_->resp0 >> 24) & 0xFF);
      cmd_req_->response[2] = (regs_->resp2 << 8) | ((regs_->resp1 >> 24) & 0xFF);
      cmd_req_->response[3] = (regs_->resp3 << 8) | ((regs_->resp2 >> 24) & 0xFF);
    } else {
      cmd_req_->response[0] = regs_->resp0;
      cmd_req_->response[1] = regs_->resp1;
      cmd_req_->response[2] = regs_->resp2;
      cmd_req_->response[3] = regs_->resp3;
    }
  } else if (cmd & (SDHCI_CMD_RESP_LEN_48 | SDHCI_CMD_RESP_LEN_48B)) {
    cmd_req_->response[0] = regs_->resp0;
    cmd_req_->response[1] = regs_->resp1;
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
    const size_t offset = (data_blockid_ * data_req_->blocksize) / sizeof(uint32_t);
    for (size_t wordid = 0; wordid < (data_req_->blocksize / sizeof(uint32_t)); wordid++) {
      *(reinterpret_cast<uint32_t*>(data_req_->virt_buffer) + offset + wordid) = regs_->data;
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
  const size_t offset = (data_blockid_ * data_req_->blocksize) / sizeof(uint32_t);
  for (size_t wordid = 0; wordid < (data_req_->blocksize / sizeof(uint32_t)); wordid++) {
    regs_->data = *(reinterpret_cast<uint32_t*>(data_req_->virt_buffer) + offset + wordid);
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
  regs_->ctrl1 |= SDHCI_SOFTWARE_RESET_CMD;
  WaitForReset(SDHCI_SOFTWARE_RESET_CMD, kResetTime);
  regs_->ctrl1 |= SDHCI_SOFTWARE_RESET_DAT;
  WaitForReset(SDHCI_SOFTWARE_RESET_DAT, kResetTime);

  // TODO(fxb/38209): data stage abort

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

    const uint32_t irq = regs_->irq;
    zxlogf(TRACE, "got irq 0x%08x 0x%08x en 0x%08x\n", regs_->irq, irq, regs_->irqen);

    // Acknowledge the IRQs that we stashed. IRQs are cleared by writing
    // 1s into the IRQs that fired.
    regs_->irq = irq;

    fbl::AutoLock lock(&mtx_);
    if (irq & SDHCI_IRQ_CMD_CPLT) {
      CmdStageCompleteLocked();
    }
    if (irq & SDHCI_IRQ_BUFF_READ_READY) {
      DataStageReadReadyLocked();
    }
    if (irq & SDHCI_IRQ_BUFF_WRITE_READY) {
      DataStageWriteReadyLocked();
    }
    if (irq & SDHCI_IRQ_XFER_CPLT) {
      TransferCompleteLocked();
    }
    if (irq & kErrorInterrupts) {
      if (driver_get_log_flags() & DDK_LOG_TRACE) {
        if (irq & SDHCI_IRQ_ERR_ADMA) {
          zxlogf(TRACE, "sdhci: ADMA error 0x%x ADMAADDR0 0x%x ADMAADDR1 0x%x\n", regs_->admaerr,
                 regs_->admaaddr0, regs_->admaaddr1);
        }
      }
      ErrorRecoveryLocked();
    }
  }
  return thrd_success;
}

zx_status_t Sdhci::BuildDmaDescriptor(sdmmc_req_t* req) {
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
  phys_iter_init(&iter, &buf, Adma64Descriptor::kMaxDescriptorLength);

  int count = 0;
  size_t length;
  zx_paddr_t paddr;
  Adma64Descriptor* desc = descs_;
  for (;;) {
    length = phys_iter_next(&iter, &paddr);
    if (length == 0) {
      if (desc != descs_) {
        desc -= 1;
        desc->end = 1;  // set end bit on the last descriptor
        break;
      } else {
        zxlogf(TRACE, "sdhci: empty descriptor list!\n");
        return ZX_ERR_NOT_SUPPORTED;
      }
    } else if (length > Adma64Descriptor::kMaxDescriptorLength) {
      zxlogf(TRACE, "sdhci: chunk size > %zu is unsupported\n", length);
      return ZX_ERR_NOT_SUPPORTED;
    } else if ((++count) > kDmaDescCount) {
      zxlogf(TRACE, "sdhci: request with more than %zd chunks is unsupported\n", length);
      return ZX_ERR_NOT_SUPPORTED;
    }
    desc->length = length & Adma64Descriptor::kDescriptorLengthMask;  // 0 = 0x10000 bytes
    desc->address = paddr;
    desc->attr = 0;
    desc->valid = 1;
    desc->act2 = 1;  // transfer data
    desc += 1;
  }

  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    desc = descs_;
    do {
      zxlogf(SPEW, "desc: addr=0x%" PRIx64 " length=0x%04x attr=0x%04x\n", desc->address,
             desc->length, desc->attr);
    } while (!(desc++)->end);
  }
  return ZX_OK;
}

zx_status_t Sdhci::StartRequestLocked(sdmmc_req_t* req) {
  const uint32_t arg = req->arg;
  const uint16_t blkcnt = req->blockcount;
  const uint16_t blksiz = req->blocksize;
  uint32_t cmd = PrepareCmd(req);
  const bool has_data = SdmmcCmdHasData(req->cmd_flags);

  if (req->use_dma && !SupportsAdma2_64Bit()) {
    zxlogf(TRACE, "sdhci: host does not support DMA\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zxlogf(TRACE, "sdhci: start_req cmd=0x%08x (data %d dma %d bsy %d) blkcnt %u blksiz %u\n", cmd,
         has_data, req->use_dma, SdmmcCmdRspBusy(req->cmd_flags), blkcnt, blksiz);

  // Every command requires that the Command Inhibit is unset.
  uint32_t inhibit_mask = SDHCI_STATE_CMD_INHIBIT;

  // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
  // it's an abort command which can be issued with the data lines active.
  if (((cmd & SDHCI_CMD_RESP_LEN_48B) == SDHCI_CMD_RESP_LEN_48B) &&
      ((cmd & SDHCI_CMD_TYPE_ABORT) == 0)) {
    inhibit_mask |= SDHCI_STATE_DAT_INHIBIT;
  }

  // Wait for the inhibit masks from above to become 0 before issuing the command.
  while (regs_->state & inhibit_mask) {
    zx::nanosleep(zx::deadline_after(kInhibitWaitTime));
  }

  if (has_data) {
    if (req->use_dma) {
      zx_status_t st = BuildDmaDescriptor(req);
      if (st != ZX_OK) {
        zxlogf(ERROR, "sdhci: failed to build DMA descriptor\n");
        return st;
      }

      zx_paddr_t desc_phys = iobuf_.phys();
      regs_->admaaddr0 = Lo32(desc_phys);
      regs_->admaaddr1 = Hi32(desc_phys);

      zxlogf(SPEW, "sdhci: descs at 0x%x 0x%x\n", regs_->admaaddr0, regs_->admaaddr1);

      cmd |= SDHCI_XFERMODE_DMA_ENABLE;
    }

    if (cmd & SDHCI_CMD_MULTI_BLK) {
      cmd |= SDHCI_CMD_AUTO12;
    }
  }

  regs_->blkcntsiz = (blksiz | (blkcnt << 16));

  regs_->arg1 = arg;

  // Clear any pending interrupts before starting the transaction.
  regs_->irq = regs_->irqen;

  // Unmask and enable interrupts
  regs_->irqen = kErrorInterrupts | kNormalInterrupts;
  regs_->irqmsk = kErrorInterrupts | kNormalInterrupts;

  // Start command
  regs_->cmd = cmd;

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

  // Disable the SD clock before messing with the voltage.
  regs_->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  switch (voltage) {
    case SDMMC_VOLTAGE_V180: {
      regs_->ctrl2 |= SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
      // 1.8V regulator out should be stable within 5ms
      zx::nanosleep(zx::deadline_after(kVoltageStabilizationTime));
      if (driver_get_log_flags() & DDK_LOG_TRACE) {
        if (!(regs_->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA)) {
          zxlogf(TRACE, "sdhci: 1.8V regulator output did not become stable\n");
          return ZX_ERR_INTERNAL;
        }
      }
      break;
    }
    case SDMMC_VOLTAGE_V330: {
      regs_->ctrl2 &= ~SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
      // 3.3V regulator out should be stable within 5ms
      zx::nanosleep(zx::deadline_after(kVoltageStabilizationTime));
      if (driver_get_log_flags() & DDK_LOG_TRACE) {
        if (regs_->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA) {
          zxlogf(TRACE, "sdhci: 3.3V regulator output did not become stable\n");
          return ZX_ERR_INTERNAL;
        }
      }
      break;
    }
    default:
      zxlogf(ERROR, "sdhci: unknown signal voltage value %u\n", voltage);
      return ZX_ERR_INVALID_ARGS;
  }

  // Make sure our changes are acknowledged.
  uint32_t expected_mask = SDHCI_PWRCTRL_SD_BUS_POWER;
  switch (voltage) {
    case SDMMC_VOLTAGE_V180:
      expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
      break;
    case SDMMC_VOLTAGE_V330:
      expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
      break;
    default:
      break;
  }
  if ((regs_->ctrl0 & expected_mask) != expected_mask) {
    zxlogf(TRACE, "sdhci: after voltage switch ctrl0=0x%08x, expected=0x%08x\n", regs_->ctrl0,
           expected_mask);
    return ZX_ERR_INTERNAL;
  }

  // Turn the clock back on
  regs_->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  zxlogf(TRACE, "sdhci: switch signal voltage to %d\n", voltage);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
  fbl::AutoLock lock(&mtx_);

  if ((bus_width == SDMMC_BUS_WIDTH_EIGHT) && !(info_.caps & SDMMC_HOST_CAP_BUS_WIDTH_8)) {
    zxlogf(TRACE, "sdhci: 8-bit bus width not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t ctrl0 = regs_->ctrl0;

  switch (bus_width) {
    case SDMMC_BUS_WIDTH_ONE:
      ctrl0 &= ~SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
      ctrl0 &= ~SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
      break;
    case SDMMC_BUS_WIDTH_FOUR:
      ctrl0 &= ~SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
      ctrl0 |= SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
      break;
    case SDMMC_BUS_WIDTH_EIGHT:
      ctrl0 &= ~SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
      ctrl0 |= SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
      break;
    default:
      zxlogf(ERROR, "sdhci: unknown bus width value %u\n", bus_width);
      return ZX_ERR_INVALID_ARGS;
  }

  regs_->ctrl0 = ctrl0;

  zxlogf(TRACE, "sdhci: set bus width to %d\n", bus_width);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetBusFreq(uint32_t bus_freq) {
  fbl::AutoLock lock(&mtx_);

  uint32_t iterations = 0;
  while (regs_->state & (SDHCI_STATE_CMD_INHIBIT | SDHCI_STATE_DAT_INHIBIT)) {
    if (++iterations > 1000) {
      return ZX_ERR_TIMED_OUT;
    }
    zx::nanosleep(zx::deadline_after(kInhibitWaitTime));
  }

  // Turn off the SD clock before messing with the clock rate.
  regs_->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  // Write the new divider into the control register.
  uint32_t ctrl1 = regs_->ctrl1;
  ctrl1 &= ~SDHCI_SD_CLOCK_FREQUENCY_SELECT_MASK;
  ctrl1 |= GetClockDividerValue(base_clock_, bus_freq);
  regs_->ctrl1 = ctrl1;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  // Turn the SD clock back on.
  regs_->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  zxlogf(TRACE, "sdhci: set bus frequency to %u\n", bus_freq);

  return ZX_OK;
}

zx_status_t Sdhci::SdmmcSetTiming(sdmmc_timing_t timing) {
  if (timing >= SDMMC_TIMING_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&mtx_);

  // Toggle high-speed
  if (timing != SDMMC_TIMING_LEGACY) {
    regs_->ctrl0 |= SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
  } else {
    regs_->ctrl0 &= ~SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
  }

  // Disable SD clock before changing UHS timing
  regs_->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  uint32_t ctrl2 = regs_->ctrl2 & ~SDHCI_HOSTCTRL2_UHS_MODE_SELECT_MASK;
  if (timing == SDMMC_TIMING_HS200) {
    ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR104;
  } else if (timing == SDMMC_TIMING_HS400) {
    ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_HS400;
  } else if (timing == SDMMC_TIMING_HSDDR) {
    ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_DDR50;
  }
  regs_->ctrl2 = ctrl2;

  // Turn the SD clock back on.
  regs_->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

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

  // TODO(fxb/38209): no other commands should run during tuning

  sdmmc_req_t req;
  {
    fbl::AutoLock lock(&mtx_);

    req = sdmmc_req_t{
        .cmd_idx = cmd_idx,
        .cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS,
        .arg = 0,
        .blockcount = 0,
        .blocksize =
            static_cast<uint16_t>((regs_->ctrl0 & SDHCI_HOSTCTRL_EXT_DATA_WIDTH) ? 128 : 64),
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

    regs_->ctrl2 |= SDHCI_HOSTCTRL2_EXEC_TUNING;
  }

  int count = 0;
  do {
    zx_status_t st = SdmmcRequest(&req);
    if (st != ZX_OK) {
      zxlogf(ERROR, "sdhci: MMC_SEND_TUNING_BLOCK error, retcode = %d\n", req.status);
      return st;
    }

    {
      fbl::AutoLock lock(&mtx_);
      if (!(regs_->ctrl2 & SDHCI_HOSTCTRL2_EXEC_TUNING)) {
        break;
      }
    }
  } while (count++ < kMaxTuningCount);

  bool fail;
  {
    fbl::AutoLock lock(&mtx_);
    fail = (regs_->ctrl2 & SDHCI_HOSTCTRL2_EXEC_TUNING) ||
           !(regs_->ctrl2 & SDHCI_HOSTCTRL2_CLOCK_SELECT);
  }

  zxlogf(TRACE, "sdhci: tuning fail %d\n", fail);

  return fail ? ZX_ERR_IO : ZX_OK;
}

zx_status_t Sdhci::SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb) {
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
  const uint16_t vrsn = (regs_->slotirqversion >> 16) & 0xff;
  if (vrsn < SDHCI_VERSION_3) {
    zxlogf(ERROR, "sdhci: SD version is %u, only version %u is supported\n", vrsn, SDHCI_VERSION_3);
    return ZX_ERR_NOT_SUPPORTED;
  }
  zxlogf(TRACE, "sdhci: controller version %d\n", vrsn);

  base_clock_ = ((regs_->caps0 >> 8) & 0xff) * kMhzToHz;
  if (base_clock_ == 0) {
    // try to get controller specific base clock
    base_clock_ = sdhci_.GetBaseClock();
  }
  if (base_clock_ == 0) {
    zxlogf(ERROR, "sdhci: base clock is 0!\n");
    return ZX_ERR_INTERNAL;
  }

  // Get controller capabilities
  uint32_t caps0 = regs_->caps0;
  if (caps0 & SDHCI_CORECFG_8_BIT_SUPPORT) {
    info_.caps |= SDMMC_HOST_CAP_BUS_WIDTH_8;
  }
  if (caps0 & SDHCI_CORECFG_ADMA2_SUPPORT && !(quirks_ & SDHCI_QUIRK_NO_DMA)) {
    info_.caps |= SDMMC_HOST_CAP_ADMA2;
  }
  if (caps0 & SDHCI_CORECFG_64BIT_SUPPORT && !(quirks_ & SDHCI_QUIRK_NO_DMA)) {
    info_.caps |= SDMMC_HOST_CAP_SIXTY_FOUR_BIT;
  }
  if (caps0 & SDHCI_CORECFG_3P3_VOLT_SUPPORT) {
    info_.caps |= SDMMC_HOST_CAP_VOLTAGE_330;
  }
  info_.caps |= SDMMC_HOST_CAP_AUTO_CMD12;

  // Set controller preferences
  if (quirks_ & SDHCI_QUIRK_NON_STANDARD_TUNING) {
    // Disable HS200 and HS400 if tuning cannot be performed as per the spec.
    info_.prefs |= SDMMC_HOST_PREFS_DISABLE_HS200 | SDMMC_HOST_PREFS_DISABLE_HS400;
  }

  // Reset the controller.
  uint32_t ctrl1 = regs_->ctrl1;

  // Perform a software reset against both the DAT and CMD interface.
  ctrl1 |= SDHCI_SOFTWARE_RESET_ALL;

  // Disable both clocks.
  ctrl1 &= ~(SDHCI_INTERNAL_CLOCK_ENABLE | SDHCI_SD_CLOCK_ENABLE);

  // Write the register back to the device.
  regs_->ctrl1 = ctrl1;

  // Wait for reset to take place. The reset is completed when all three
  // of the following flags are reset.
  const uint32_t target_mask =
      (SDHCI_SOFTWARE_RESET_ALL | SDHCI_SOFTWARE_RESET_CMD | SDHCI_SOFTWARE_RESET_DAT);
  zx_status_t status = ZX_OK;
  if ((status = WaitForReset(target_mask, kResetTime)) != ZX_OK) {
    return status;
  }

  // allocate and setup DMA descriptor
  if (SupportsAdma2_64Bit()) {
    status = iobuf_.Init(bti_.get(), kDmaDescCount * sizeof(Adma64Descriptor),
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      zxlogf(ERROR, "sdhci: error allocating DMA descriptors\n");
      return status;
    }
    descs_ = reinterpret_cast<Adma64Descriptor*>(iobuf_.virt());
    info_.max_transfer_size = kDmaDescCount * PAGE_SIZE;

    // Select ADMA2
    regs_->ctrl0 |= SDHCI_HOSTCTRL_DMA_SELECT_ADMA2;
  } else {
    // no maximum if only PIO supported
    info_.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
  }
  info_.max_transfer_size_non_dma = BLOCK_MAX_TRANSFER_UNBOUNDED;

  // Configure the clock.
  ctrl1 = regs_->ctrl1;
  ctrl1 |= SDHCI_INTERNAL_CLOCK_ENABLE;

  // SDHCI Versions 1.00 and 2.00 handle the clock divider slightly
  // differently compared to SDHCI version 3.00. Since this driver doesn't
  // support SDHCI versions < 3.00, we ignore this incongruency for now.
  //
  // V3.00 supports a 10 bit divider where the SD clock frequency is defined
  // as F/(2*D) where F is the base clock frequency and D is the divider.
  ctrl1 &= ~SDHCI_SD_CLOCK_FREQUENCY_SELECT_MASK;
  ctrl1 |= GetClockDividerValue(base_clock_, kSdFreqSetupHz);

  // Set the command timeout.
  ctrl1 |= (0xe << 16);

  // Write back the clock frequency, command timeout and clock enable bits.
  regs_->ctrl1 = ctrl1;

  // Wait for the clock to stabilize.
  zx::time deadline = zx::clock::get_monotonic() + kClockStabilizationTime;
  while (true) {
    if (((regs_->ctrl1) & SDHCI_INTERNAL_CLOCK_STABLE) != 0)
      break;

    if (zx::clock::get_monotonic() > deadline) {
      zxlogf(ERROR, "sdhci: Clock did not stabilize in time\n");
      return ZX_ERR_TIMED_OUT;
    }
  }

  // Cut voltage to the card. This may automatically gate the SD clock on some controllers.
  regs_->ctrl0 &= ~SDHCI_PWRCTRL_SD_BUS_POWER;

  // Set SD bus voltage to maximum supported by the host controller
  uint32_t ctrl0 = regs_->ctrl0 & ~SDHCI_PWRCTRL_SD_BUS_VOLTAGE_MASK;
  if (info_.caps & SDMMC_HOST_CAP_VOLTAGE_330) {
    ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
  } else {
    ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
  }
  regs_->ctrl0 = ctrl0;

  // Restore voltage to the card.
  regs_->ctrl0 |= SDHCI_PWRCTRL_SD_BUS_POWER;

  // Enable the SD clock.
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));
  ctrl1 = regs_->ctrl1;
  ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
  regs_->ctrl1 = ctrl1;
  zx::nanosleep(zx::deadline_after(kControlUpdateWaitTime));

  // Disable all interrupts
  regs_->irqen = 0;
  regs_->irq = 0xffffffff;

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
  status = ddk::MmioBuffer::Create(vmo_offset, sizeof(*Sdhci::regs_), std::move(vmo),
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
