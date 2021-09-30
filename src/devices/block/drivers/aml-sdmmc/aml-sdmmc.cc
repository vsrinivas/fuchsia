// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-sdmmc.h"

#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/ddk/phys-iter.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fit/defer.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/sdmmc/hw.h>
#include <lib/sync/completion.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <algorithm>
#include <string>

#include <bits/limits.h>
#include <fbl/algorithm.h>
#include <soc/aml-common/aml-sdmmc.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "aml-sdmmc-regs.h"
#include "src/devices/block/drivers/aml-sdmmc/aml-sdmmc-bind.h"

// Limit maximum number of descriptors to 512 for now
#define AML_DMA_DESC_MAX_COUNT 512
#define AML_SDMMC_TRACE(fmt, ...) zxlogf(DEBUG, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SDMMC_INFO(fmt, ...) zxlogf(INFO, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SDMMC_ERROR(fmt, ...) zxlogf(ERROR, "%s: " fmt, __func__, ##__VA_ARGS__)

namespace {

uint32_t log2_ceil(uint32_t blk_sz) {
  if (blk_sz == 1) {
    return 0;
  }
  return 32 - (__builtin_clz(blk_sz - 1));
}

zx_paddr_t PageMask() {
  static uintptr_t page_size = zx_system_get_page_size();
  return page_size - 1;
}

}  // namespace

namespace sdmmc {

AmlSdmmc::AmlSdmmc(zx_device_t* parent, zx::bti bti, ddk::MmioBuffer mmio,
                   ddk::MmioPinnedBuffer pinned_mmio, aml_sdmmc_config_t config, zx::interrupt irq,
                   const ddk::GpioProtocolClient& gpio)
    : AmlSdmmcType(parent),
      mmio_(std::move(mmio)),
      bti_(std::move(bti)),
      pinned_mmio_(std::move(pinned_mmio)),
      reset_gpio_(gpio),
      irq_(std::move(irq)),
      board_config_(config),
      dead_(false),
      pending_txn_(false) {
  for (auto& store : registered_vmos_) {
    store.emplace(vmo_store::Options{});
  }
}

zx_status_t AmlSdmmc::WaitForInterruptImpl() {
  zx::time timestamp;
  return irq_.wait(&timestamp);
}

void AmlSdmmc::ClearStatus() {
  AmlSdmmcStatus::Get()
      .ReadFrom(&mmio_)
      .set_reg_value(AmlSdmmcStatus::kClearStatus)
      .WriteTo(&mmio_);
}

void AmlSdmmc::Inspect::Init(const pdev_device_info_t& device_info) {
  std::string root_name = "aml-sdmmc-port";
  if (device_info.did == PDEV_DID_AMLOGIC_SDMMC_A) {
    root_name += 'A';
  } else if (device_info.did == PDEV_DID_AMLOGIC_SDMMC_B) {
    root_name += 'B';
  } else if (device_info.did == PDEV_DID_AMLOGIC_SDMMC_C) {
    root_name += 'C';
  } else {
    root_name += "-unknown";
  }

  root = inspector.GetRoot().CreateChild(root_name);

  bus_clock_frequency = root.CreateUint(
      "bus_clock_frequency", AmlSdmmcClock::kCtsOscinClkFreq / AmlSdmmcClock::kDefaultClkDiv);
  tx_clock_phase = root.CreateUint("tx_clock_phase", AmlSdmmcClock::kDefaultClkTxPhase);
  adj_delay = root.CreateUint("adj_delay", 0);
  delay_lines = root.CreateUint("delay_lines", 0);
  tuning_results = root.CreateString("tuning_results", "none");
  delay_window_size = root.CreateUint("delay_window_size", 0);
  max_delay = root.CreateUint("max_delay", 0);
}

zx_status_t AmlSdmmc::WaitForInterrupt(sdmmc_req_t* req) {
  zx_status_t status = WaitForInterruptImpl();

  if (status != ZX_OK) {
    AML_SDMMC_ERROR("WaitForInterruptImpl got %d", status);
    return status;
  }

  const auto status_irq = AmlSdmmcStatus::Get().ReadFrom(&mmio_);

  auto complete = fit::defer([&]() { ClearStatus(); });

  auto on_bus_error =
      fit::defer([&]() { AmlSdmmcStart::Get().ReadFrom(&mmio_).set_desc_busy(0).WriteTo(&mmio_); });

  if (status_irq.rxd_err()) {
    if (req->suppress_error_messages) {
      AML_SDMMC_TRACE("RX Data CRC Error cmd%d, arg=0x%08x, status=0x%08x", req->cmd_idx, req->arg,
                      status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("RX Data CRC Error cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req->cmd_idx, req->arg, status_irq.reg_value(), ++consecutive_data_errors_);
    }
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (status_irq.txd_err()) {
    AML_SDMMC_ERROR("TX Data CRC Error, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                    req->cmd_idx, req->arg, status_irq.reg_value(), ++consecutive_data_errors_);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (status_irq.desc_err()) {
    AML_SDMMC_ERROR("Controller does not own the descriptor, cmd%d, arg=0x%08x, status=0x%08x",
                    req->cmd_idx, req->arg, status_irq.reg_value());
    return ZX_ERR_IO_INVALID;
  }
  if (status_irq.resp_err()) {
    if (req->suppress_error_messages) {
      AML_SDMMC_TRACE("Response CRC Error, cmd%d, arg=0x%08x, status=0x%08x", req->cmd_idx,
                      req->arg, status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("Response CRC Error, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req->cmd_idx, req->arg, status_irq.reg_value(), ++consecutive_cmd_errors_);
    }
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  if (status_irq.resp_timeout()) {
    // A timeout is acceptable for SD_SEND_IF_COND but not for MMC_SEND_EXT_CSD.
    const bool is_sd_cmd8 =
        req->cmd_idx == SD_SEND_IF_COND && req->cmd_flags == SD_SEND_IF_COND_FLAGS;
    static_assert(SD_SEND_IF_COND == MMC_SEND_EXT_CSD &&
                  (SD_SEND_IF_COND_FLAGS) != (MMC_SEND_EXT_CSD_FLAGS));
    // When mmc dev_ice is being probed with SDIO command this is an expected failure.
    if (req->suppress_error_messages || is_sd_cmd8) {
      AML_SDMMC_TRACE("Response timeout, cmd%d, arg=0x%08x, status=0x%08x", req->cmd_idx, req->arg,
                      status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("Reponse timeout, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req->cmd_idx, req->arg, status_irq.reg_value(), ++consecutive_cmd_errors_);
    }
    return ZX_ERR_TIMED_OUT;
  }
  if (status_irq.desc_timeout()) {
    AML_SDMMC_ERROR("Descriptor timeout, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                    req->cmd_idx, req->arg, status_irq.reg_value(), ++consecutive_data_errors_);
    return ZX_ERR_TIMED_OUT;
  }

  if (!(status_irq.end_of_chain())) {
    AML_SDMMC_ERROR("END OF CHAIN bit is not set, cmd%d, arg=0x%08x, status=0x%08x", req->cmd_idx,
                    req->arg, status_irq.reg_value());
    return ZX_ERR_IO_INVALID;
  }

  // At this point we have succeeded and don't need to perform our on-error call
  on_bus_error.cancel();

  consecutive_cmd_errors_ = 0;
  if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    consecutive_data_errors_ = 0;
  }

  if (req->cmd_flags & SDMMC_RESP_LEN_136) {
    req->response[0] = AmlSdmmcCmdResp::Get().ReadFrom(&mmio_).reg_value();
    req->response[1] = AmlSdmmcCmdResp1::Get().ReadFrom(&mmio_).reg_value();
    req->response[2] = AmlSdmmcCmdResp2::Get().ReadFrom(&mmio_).reg_value();
    req->response[3] = AmlSdmmcCmdResp3::Get().ReadFrom(&mmio_).reg_value();
  } else {
    req->response[0] = AmlSdmmcCmdResp::Get().ReadFrom(&mmio_).reg_value();
  }
  if ((!req->use_dma) && (req->cmd_flags & SDMMC_CMD_READ)) {
    uint32_t length = req->blockcount * req->blocksize;
    if (length == 0 || ((length % 4) != 0)) {
      return ZX_ERR_INTERNAL;
    }
    uint32_t* dest = reinterpret_cast<uint32_t*>(req->virt_buffer);
    volatile uint32_t* src = reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uintptr_t>(mmio_.get()) + kAmlSdmmcPingOffset);
    while (length) {
      *dest++ = *src++;
      length -= 4;
    }
  }

  return ZX_OK;
}

zx::status<std::array<uint32_t, AmlSdmmc::kResponseCount>> AmlSdmmc::WaitForInterruptNew(
    const sdmmc_req_new_t& req) {
  zx_status_t status = WaitForInterruptImpl();

  if (status != ZX_OK) {
    AML_SDMMC_ERROR("WaitForInterruptImpl got %d", status);
    return zx::error(status);
  }

  const auto status_irq = AmlSdmmcStatus::Get().ReadFrom(&mmio_);

  auto complete = fit::defer([&]() { ClearStatus(); });

  auto on_bus_error =
      fit::defer([&]() { AmlSdmmcStart::Get().ReadFrom(&mmio_).set_desc_busy(0).WriteTo(&mmio_); });

  if (status_irq.rxd_err()) {
    if (req.suppress_error_messages) {
      AML_SDMMC_TRACE("RX Data CRC Error cmd%d, arg=0x%08x, status=0x%08x", req.cmd_idx, req.arg,
                      status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("RX Data CRC Error cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req.cmd_idx, req.arg, status_irq.reg_value(), ++consecutive_data_errors_);
    }
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  if (status_irq.txd_err()) {
    AML_SDMMC_ERROR("TX Data CRC Error, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                    req.cmd_idx, req.arg, status_irq.reg_value(), ++consecutive_data_errors_);
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  if (status_irq.desc_err()) {
    AML_SDMMC_ERROR("Controller does not own the descriptor, cmd%d, arg=0x%08x, status=0x%08x",
                    req.cmd_idx, req.arg, status_irq.reg_value());
    return zx::error(ZX_ERR_IO_INVALID);
  }
  if (status_irq.resp_err()) {
    if (req.suppress_error_messages) {
      AML_SDMMC_TRACE("Response CRC Error, cmd%d, arg=0x%08x, status=0x%08x", req.cmd_idx, req.arg,
                      status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("Response CRC Error, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req.cmd_idx, req.arg, status_irq.reg_value(), ++consecutive_cmd_errors_);
    }
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }
  if (status_irq.resp_timeout()) {
    // A timeout is acceptable for SD_SEND_IF_COND but not for MMC_SEND_EXT_CSD.
    const bool is_sd_cmd8 =
        req.cmd_idx == SD_SEND_IF_COND && req.cmd_flags == SD_SEND_IF_COND_FLAGS;
    static_assert(SD_SEND_IF_COND == MMC_SEND_EXT_CSD &&
                  (SD_SEND_IF_COND_FLAGS) != (MMC_SEND_EXT_CSD_FLAGS));
    // When mmc dev_ice is being probed with SDIO command this is an expected failure.
    if (req.suppress_error_messages || is_sd_cmd8) {
      AML_SDMMC_TRACE("Response timeout, cmd%d, arg=0x%08x, status=0x%08x", req.cmd_idx, req.arg,
                      status_irq.reg_value());
    } else {
      AML_SDMMC_ERROR("Response timeout, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                      req.cmd_idx, req.arg, status_irq.reg_value(), ++consecutive_cmd_errors_);
    }
    return zx::error(ZX_ERR_TIMED_OUT);
  }
  if (status_irq.desc_timeout()) {
    AML_SDMMC_ERROR("Descriptor timeout, cmd%d, arg=0x%08x, status=0x%08x, consecutive=%lu",
                    req.cmd_idx, req.arg, status_irq.reg_value(), ++consecutive_data_errors_);
    return zx::error(ZX_ERR_TIMED_OUT);
  }

  if (!(status_irq.end_of_chain())) {
    AML_SDMMC_ERROR("END OF CHAIN bit is not set, cmd%d, arg=0x%08x, status=0x%08x", req.cmd_idx,
                    req.arg, status_irq.reg_value());
    return zx::error(ZX_ERR_IO_INVALID);
  }

  // At this point we have succeeded and don't need to perform our on-error call
  on_bus_error.cancel();

  consecutive_cmd_errors_ = 0;
  if (req.cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    consecutive_data_errors_ = 0;
  }

  std::array<uint32_t, AmlSdmmc::kResponseCount> response = {};
  if (req.cmd_flags & SDMMC_RESP_LEN_136) {
    response[0] = AmlSdmmcCmdResp::Get().ReadFrom(&mmio_).reg_value();
    response[1] = AmlSdmmcCmdResp1::Get().ReadFrom(&mmio_).reg_value();
    response[2] = AmlSdmmcCmdResp2::Get().ReadFrom(&mmio_).reg_value();
    response[3] = AmlSdmmcCmdResp3::Get().ReadFrom(&mmio_).reg_value();
  } else {
    response[0] = AmlSdmmcCmdResp::Get().ReadFrom(&mmio_).reg_value();
  }

  return zx::ok(response);
}

zx_status_t AmlSdmmc::SdmmcHostInfo(sdmmc_host_info_t* info) {
  dev_info_.prefs = board_config_.prefs;
  memcpy(info, &dev_info_, sizeof(dev_info_));
  return ZX_OK;
}

zx_status_t AmlSdmmc::SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
  uint32_t bus_width_val;
  switch (bus_width) {
    case SDMMC_BUS_WIDTH_EIGHT:
      bus_width_val = AmlSdmmcCfg::kBusWidth8Bit;
      break;
    case SDMMC_BUS_WIDTH_FOUR:
      bus_width_val = AmlSdmmcCfg::kBusWidth4Bit;
      break;
    case SDMMC_BUS_WIDTH_ONE:
      bus_width_val = AmlSdmmcCfg::kBusWidth1Bit;
      break;
    default:
      return ZX_ERR_OUT_OF_RANGE;
  }

  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_bus_width(bus_width_val).WriteTo(&mmio_);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  return ZX_OK;
}

zx_status_t AmlSdmmc::SdmmcRegisterInBandInterrupt(
    const in_band_interrupt_protocol_t* interrupt_cb) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlSdmmc::SdmmcSetBusFreq(uint32_t freq) {
  uint32_t clk = 0, clk_src = 0, clk_div = 0;
  if (freq == 0) {
    AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(0).WriteTo(&mmio_);
    inspect_.bus_clock_frequency.Set(0);
    return ZX_OK;
  }

  if (freq > max_freq_) {
    freq = max_freq_;
  } else if (freq < min_freq_) {
    freq = min_freq_;
  }
  if (freq < AmlSdmmcClock::kFClkDiv2MinFreq) {
    clk_src = AmlSdmmcClock::kCtsOscinClkSrc;
    clk = AmlSdmmcClock::kCtsOscinClkFreq;
  } else {
    clk_src = AmlSdmmcClock::kFClkDiv2Src;
    clk = AmlSdmmcClock::kFClkDiv2Freq;
  }
  // Round the divider up so the frequency is rounded down.
  clk_div = (clk + freq - 1) / freq;
  AmlSdmmcClock::Get().ReadFrom(&mmio_).set_cfg_div(clk_div).set_cfg_src(clk_src).WriteTo(&mmio_);
  inspect_.bus_clock_frequency.Set(clk / clk_div);
  return ZX_OK;
}

void AmlSdmmc::ConfigureDefaultRegs() {
  if (board_config_.version_3) {
    uint32_t clk_val = AmlSdmmcClockV3::Get()
                           .FromValue(0)
                           .set_cfg_div(AmlSdmmcClock::kDefaultClkDiv)
                           .set_cfg_src(AmlSdmmcClock::kDefaultClkSrc)
                           .set_cfg_co_phase(AmlSdmmcClock::kDefaultClkCorePhase)
                           .set_cfg_tx_phase(AmlSdmmcClock::kDefaultClkTxPhase)
                           .set_cfg_rx_phase(AmlSdmmcClock::kDefaultClkRxPhase)
                           .set_cfg_always_on(1)
                           .reg_value();
    AmlSdmmcClockV3::Get().ReadFrom(&mmio_).set_reg_value(clk_val).WriteTo(&mmio_);
  } else {
    uint32_t clk_val = AmlSdmmcClockV2::Get()
                           .FromValue(0)
                           .set_cfg_div(AmlSdmmcClock::kDefaultClkDiv)
                           .set_cfg_src(AmlSdmmcClock::kDefaultClkSrc)
                           .set_cfg_co_phase(AmlSdmmcClock::kDefaultClkCorePhase)
                           .set_cfg_tx_phase(AmlSdmmcClock::kDefaultClkTxPhase)
                           .set_cfg_rx_phase(AmlSdmmcClock::kDefaultClkRxPhase)
                           .set_cfg_always_on(1)
                           .reg_value();
    AmlSdmmcClockV2::Get().ReadFrom(&mmio_).set_reg_value(clk_val).WriteTo(&mmio_);
  }

  uint32_t config_val = AmlSdmmcCfg::Get()
                            .FromValue(0)
                            .set_blk_len(AmlSdmmcCfg::kDefaultBlkLen)
                            .set_resp_timeout(AmlSdmmcCfg::kDefaultRespTimeout)
                            .set_rc_cc(AmlSdmmcCfg::kDefaultRcCc)
                            .set_bus_width(AmlSdmmcCfg::kBusWidth1Bit)
                            .reg_value();
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_reg_value(config_val).WriteTo(&mmio_);
  AmlSdmmcStatus::Get()
      .ReadFrom(&mmio_)
      .set_reg_value(AmlSdmmcStatus::kClearStatus)
      .WriteTo(&mmio_);
  AmlSdmmcIrqEn::Get().ReadFrom(&mmio_).set_reg_value(AmlSdmmcStatus::kClearStatus).WriteTo(&mmio_);

  // Zero out any delay line or sampling settings that may have come from the bootloader.
  if (board_config_.version_3) {
    AmlSdmmcAdjust::Get().FromValue(0).WriteTo(&mmio_);
    AmlSdmmcDelay1::Get().FromValue(0).WriteTo(&mmio_);
    AmlSdmmcDelay2::Get().FromValue(0).WriteTo(&mmio_);
  } else {
    AmlSdmmcAdjustV2::Get().FromValue(0).WriteTo(&mmio_);
    AmlSdmmcDelayV2::Get().FromValue(0).WriteTo(&mmio_);
  }
}

void AmlSdmmc::SdmmcHwReset() {
  if (reset_gpio_.is_valid()) {
    reset_gpio_.ConfigOut(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    reset_gpio_.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  }
  ConfigureDefaultRegs();
}

zx_status_t AmlSdmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
  auto config = AmlSdmmcCfg::Get().ReadFrom(&mmio_);
  if (timing == SDMMC_TIMING_HS400 || timing == SDMMC_TIMING_HSDDR ||
      timing == SDMMC_TIMING_DDR50) {
    if (timing == SDMMC_TIMING_HS400) {
      config.set_chk_ds(1);
    } else {
      config.set_chk_ds(0);
    }
    config.set_ddr(1);
    auto clk = AmlSdmmcClock::Get().ReadFrom(&mmio_);
    uint32_t clk_div = clk.cfg_div();
    if (clk_div & 0x01) {
      clk_div++;
    }
    clk_div /= 2;
    clk.set_cfg_div(clk_div).WriteTo(&mmio_);
  } else {
    config.set_ddr(0);
  }

  config.WriteTo(&mmio_);
  return ZX_OK;
}

zx_status_t AmlSdmmc::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
  // Amlogic controller does not allow to modify voltage
  // We do not return an error here since things work fine without switching the voltage.
  return ZX_OK;
}

void AmlSdmmc::SetupCmdDesc(sdmmc_req_t* req, aml_sdmmc_desc_t** out_desc) {
  aml_sdmmc_desc_t* desc;
  if (req->use_dma) {
    ZX_DEBUG_ASSERT((dev_info_.caps & SDMMC_HOST_CAP_DMA));
    desc = reinterpret_cast<aml_sdmmc_desc_t*>(descs_buffer_.virt());
    memset(desc, 0, descs_buffer_.size());
  } else {
    desc = reinterpret_cast<aml_sdmmc_desc_t*>(reinterpret_cast<uintptr_t>(mmio_.get()) +
                                               AML_SDMMC_SRAM_MEMORY_BASE);
  }
  auto cmd_cfg = AmlSdmmcCmdCfg::Get().FromValue(0);
  if (req->cmd_flags == 0) {
    cmd_cfg.set_no_resp(1);
  } else {
    if (req->cmd_flags & SDMMC_RESP_LEN_136) {
      cmd_cfg.set_resp_128(1);
    }

    if (!(req->cmd_flags & SDMMC_RESP_CRC_CHECK)) {
      cmd_cfg.set_resp_no_crc(1);
    }

    if (req->cmd_flags & SDMMC_RESP_LEN_48B) {
      cmd_cfg.set_r1b(1);
    }

    cmd_cfg.set_resp_num(1);
  }
  cmd_cfg.set_cmd_idx(req->cmd_idx)
      .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
      .set_error(0)
      .set_owner(1)
      .set_end_of_chain(0);

  desc->cmd_info = cmd_cfg.reg_value();
  desc->cmd_arg = req->arg;
  desc->data_addr = 0;
  desc->resp_addr = 0;
  *out_desc = desc;
}

aml_sdmmc_desc_t* AmlSdmmc::SetupCmdDescNew(const sdmmc_req_new_t& req) {
  aml_sdmmc_desc_t* const desc = reinterpret_cast<aml_sdmmc_desc_t*>(descs_buffer_.virt());
  auto cmd_cfg = AmlSdmmcCmdCfg::Get().FromValue(0);
  if (req.cmd_flags == 0) {
    cmd_cfg.set_no_resp(1);
  } else {
    if (req.cmd_flags & SDMMC_RESP_LEN_136) {
      cmd_cfg.set_resp_128(1);
    }

    if (!(req.cmd_flags & SDMMC_RESP_CRC_CHECK)) {
      cmd_cfg.set_resp_no_crc(1);
    }

    if (req.cmd_flags & SDMMC_RESP_LEN_48B) {
      cmd_cfg.set_r1b(1);
    }

    cmd_cfg.set_resp_num(1);
  }
  cmd_cfg.set_cmd_idx(req.cmd_idx)
      .set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout)
      .set_error(0)
      .set_owner(1)
      .set_end_of_chain(0);

  desc->cmd_info = cmd_cfg.reg_value();
  desc->cmd_arg = req.arg;
  desc->data_addr = 0;
  desc->resp_addr = 0;
  return desc;
}

zx_status_t AmlSdmmc::SetupDataDescsDma(sdmmc_req_t* req, aml_sdmmc_desc_t* cur_desc,
                                        aml_sdmmc_desc_t** last_desc) {
  uint64_t req_len = req->blockcount * req->blocksize;
  bool is_read = req->cmd_flags & SDMMC_CMD_READ;
  uint64_t pagecount =
      ((req->buf_offset & PageMask()) + req_len + PageMask()) / zx_system_get_page_size();
  if (pagecount > SDMMC_PAGES_COUNT) {
    AML_SDMMC_ERROR("too many pages %" PRIu64 " vs %" PRIu64, pagecount, SDMMC_PAGES_COUNT);
    return ZX_ERR_INVALID_ARGS;
  }

  // pin the vmo
  zx_paddr_t phys[SDMMC_PAGES_COUNT];
  // offset_vmo is converted to bytes by the sdmmc layer
  uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

  zx_status_t st = zx_bti_pin(bti_.get(), options, req->dma_vmo, req->buf_offset & ~PageMask(),
                              pagecount * zx_system_get_page_size(), phys, pagecount, &req->pmt);
  if (st != ZX_OK) {
    AML_SDMMC_ERROR("bti-pin failed with error %d", st);
    return st;
  }

  auto unpin = fit::defer([&req]() { zx_pmt_unpin(req->pmt); });
  if (is_read) {
    st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset, req_len,
                         nullptr, 0);
  } else {
    st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN, req->buf_offset, req_len, nullptr, 0);
  }
  if (st != ZX_OK) {
    AML_SDMMC_ERROR("cache clean failed with error  %d", st);
    return st;
  }

  phys_iter_buffer_t buf = {};
  buf.phys = phys;
  buf.phys_count = pagecount;
  buf.length = req_len;
  buf.vmo_offset = req->buf_offset;

  phys_iter_t iter;
  phys_iter_init(&iter, &buf, zx_system_get_page_size());

  int count = 0;
  size_t length;
  zx_paddr_t paddr;
  uint16_t blockcount;
  aml_sdmmc_desc_t* desc = cur_desc;
  for (;;) {
    length = phys_iter_next(&iter, &paddr);
    if (length == 0) {
      if (desc != descs_buffer_.virt()) {
        desc -= 1;
        *last_desc = desc;
        break;
      }

      AML_SDMMC_ERROR("empty descriptor list!");
      return ZX_ERR_NOT_SUPPORTED;
    }
    if (length > zx_system_get_page_size()) {
      AML_SDMMC_ERROR("chunk size > %zu is unsupported", length);
      return ZX_ERR_NOT_SUPPORTED;
    }
    if ((++count) > AML_DMA_DESC_MAX_COUNT) {
      AML_SDMMC_ERROR("request with more than %d chunks is unsupported\n", AML_DMA_DESC_MAX_COUNT);
      return ZX_ERR_NOT_SUPPORTED;
    }
    auto cmd = AmlSdmmcCmdCfg::Get().FromValue(desc->cmd_info);
    if (count > 1) {
      cmd.set_no_resp(1).set_no_cmd(1);
    }

    cmd.set_data_io(1);
    if (!(req->cmd_flags & SDMMC_CMD_READ)) {
      cmd.set_data_wr(1);
    }
    cmd.set_owner(1).set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout).set_error(0);

    uint16_t blocksize = req->blocksize;
    blockcount = static_cast<uint16_t>(length / blocksize);
    ZX_DEBUG_ASSERT(((length % blocksize) == 0));

    if (blockcount > 1) {
      cmd.set_block_mode(1).set_length(blockcount);
    } else {
      cmd.set_length(req->blocksize);
    }

    desc->cmd_info = cmd.reg_value();
    desc->data_addr = static_cast<uint32_t>(paddr);
    desc += 1;
  }
  unpin.cancel();
  return ZX_OK;
}

zx_status_t AmlSdmmc::SetupDataDescsPio(sdmmc_req_t* req, aml_sdmmc_desc_t* desc,
                                        aml_sdmmc_desc_t** last_desc) {
  zx_status_t status = ZX_OK;
  uint32_t length = req->blockcount * req->blocksize;

  if (length > AML_SDMMC_MAX_PIO_DATA_SIZE) {
    AML_SDMMC_ERROR("Request transfer size is greater than max transfer size");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (length == 0 || ((length % 4) != 0)) {
    // From Amlogic documentation, Ping and Pong buffers in sram can be accessed only 4 bytes
    // at a time.
    AML_SDMMC_ERROR("Request sizes that are not multiple of 4 are not supported in PIO mode");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto cmd = AmlSdmmcCmdCfg::Get().FromValue(desc->cmd_info);
  cmd.set_data_io(1);
  if (!(req->cmd_flags & SDMMC_CMD_READ)) {
    cmd.set_data_wr(1);
    uint32_t data_remaining = length;
    uint32_t* src = reinterpret_cast<uint32_t*>(req->virt_buffer);
    volatile uint32_t* dest = reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uintptr_t>(mmio_.get()) + kAmlSdmmcPingOffset);
    while (data_remaining) {
      *dest++ = *src++;
      data_remaining -= 4;
    }
  }

  if (req->blockcount > 1) {
    cmd.set_block_mode(1).set_length(req->blockcount);
  } else {
    cmd.set_length(req->blocksize);
  }

  // data_addr[0] = 0 for DDR. data_addr[0] = 1 if address is from SRAM

  desc->cmd_info = cmd.reg_value();
  zx_paddr_t buffer_phys = pinned_mmio_.get_paddr() + kAmlSdmmcPingOffset;
  desc->data_addr = static_cast<uint32_t>(buffer_phys | 1);
  *last_desc = desc;
  return status;
}

zx_status_t AmlSdmmc::SetupDataDescs(sdmmc_req_t* req, aml_sdmmc_desc_t* desc,
                                     aml_sdmmc_desc_t** last_desc) {
  zx_status_t st = ZX_OK;

  if (!req->blocksize || req->blocksize > AmlSdmmcCmdCfg::kMaxBlockSize) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (req->use_dma) {
    st = SetupDataDescsDma(req, desc, last_desc);
    if (st != ZX_OK) {
      return st;
    }
  } else {
    st = SetupDataDescsPio(req, desc, last_desc);
    if (st != ZX_OK) {
      return st;
    }
  }

  // update config
  uint8_t cur_blk_len = static_cast<uint8_t>(AmlSdmmcCfg::Get().ReadFrom(&mmio_).blk_len());
  uint8_t req_blk_len = static_cast<uint8_t>(log2_ceil(req->blocksize));
  if (cur_blk_len != req_blk_len) {
    AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_blk_len(req_blk_len).WriteTo(&mmio_);
  }
  return ZX_OK;
}

zx::status<std::pair<aml_sdmmc_desc_t*, std::vector<fzl::PinnedVmo>>> AmlSdmmc::SetupDataDescsNew(
    const sdmmc_req_new_t& req, aml_sdmmc_desc_t* const cur_desc) {
  const uint32_t req_blk_len = log2_ceil(req.blocksize);
  if (req_blk_len > AmlSdmmcCfg::kMaxBlkLen) {
    AML_SDMMC_ERROR("blocksize %u is greater than the max (%u)", 1 << req_blk_len,
                    1 << AmlSdmmcCfg::kMaxBlkLen);
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  AmlSdmmcCfg::Get().ReadFrom(&mmio_).set_blk_len(req_blk_len).WriteTo(&mmio_);

  std::vector<fzl::PinnedVmo> pinned_vmos;
  pinned_vmos.reserve(req.buffers_count);

  aml_sdmmc_desc_t* desc = cur_desc;
  SdmmcVmoStore& vmos = *registered_vmos_[req.client_id];
  for (size_t i = 0; i < req.buffers_count; i++) {
    if (req.buffers_list[i].type == SDMMC_BUFFER_TYPE_VMO_HANDLE) {
      auto status = SetupUnownedVmoDescs(req, req.buffers_list[i], desc);
      if (!status.is_ok()) {
        return zx::error(status.error_value());
      }

      pinned_vmos.push_back(std::move(std::get<1>(status.value())));
      desc = std::get<0>(status.value());
    } else {
      vmo_store::StoredVmo<OwnedVmoInfo>* const stored_vmo =
          vmos.GetVmo(req.buffers_list[i].buffer.vmo_id);
      if (stored_vmo == nullptr) {
        AML_SDMMC_ERROR("no VMO %u for client %u", req.buffers_list[i].buffer.vmo_id,
                        req.client_id);
        return zx::error(ZX_ERR_NOT_FOUND);
      }
      auto status = SetupOwnedVmoDescs(req, req.buffers_list[i], *stored_vmo, desc);
      if (status.is_error()) {
        return zx::error(status.error_value());
      }
      desc = status.value();
    }
  }

  if (desc == cur_desc) {
    AML_SDMMC_ERROR("empty descriptor list!");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok(std::pair{desc - 1, std::move(pinned_vmos)});
}

zx::status<aml_sdmmc_desc_t*> AmlSdmmc::SetupOwnedVmoDescs(const sdmmc_req_new_t& req,
                                                           const sdmmc_buffer_region_t& buffer,
                                                           vmo_store::StoredVmo<OwnedVmoInfo>& vmo,
                                                           aml_sdmmc_desc_t* const cur_desc) {
  if (!(req.cmd_flags & SDMMC_CMD_READ) && !(vmo.meta().rights & SDMMC_VMO_RIGHT_READ)) {
    AML_SDMMC_ERROR("Request would read from write-only VMO");
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  if ((req.cmd_flags & SDMMC_CMD_READ) && !(vmo.meta().rights & SDMMC_VMO_RIGHT_WRITE)) {
    AML_SDMMC_ERROR("Request would write to read-only VMO");
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }

  if (buffer.offset + buffer.size > vmo.meta().size) {
    AML_SDMMC_ERROR("buffer reads past vmo end: offset %zu, size %zu, vmo size %zu",
                    buffer.offset + vmo.meta().offset, buffer.size, vmo.meta().size);
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  fzl::PinnedVmo::Region regions[SDMMC_PAGES_COUNT];
  size_t offset = buffer.offset;
  size_t remaining = buffer.size;
  aml_sdmmc_desc_t* desc = cur_desc;
  while (remaining > 0) {
    size_t region_count = 0;
    zx_status_t status = vmo.GetPinnedRegions(offset + vmo.meta().offset, buffer.size, regions,
                                              countof(regions), &region_count);
    if (status != ZX_OK && status != ZX_ERR_BUFFER_TOO_SMALL) {
      AML_SDMMC_ERROR("failed to get pinned regions: %d", status);
      return zx::error(status);
    }

    const size_t last_offset = offset;
    for (size_t i = 0; i < region_count; i++) {
      zx::status<aml_sdmmc_desc_t*> next_desc = PopulateDescriptors(req, desc, regions[i]);
      if (next_desc.is_error()) {
        return next_desc;
      }

      desc = next_desc.value();
      offset += regions[i].size;
      remaining -= regions[i].size;
    }

    if (offset == last_offset) {
      AML_SDMMC_ERROR("didn't get any pinned regions");
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }

  return zx::ok(desc);
}

zx::status<std::pair<aml_sdmmc_desc_t*, fzl::PinnedVmo>> AmlSdmmc::SetupUnownedVmoDescs(
    const sdmmc_req_new_t& req, const sdmmc_buffer_region_t& buffer,
    aml_sdmmc_desc_t* const cur_desc) {
  const bool is_read = req.cmd_flags & SDMMC_CMD_READ;
  const uint64_t pagecount =
      ((buffer.offset & PageMask()) + buffer.size + PageMask()) / zx_system_get_page_size();

  const zx::unowned_vmo vmo(buffer.buffer.vmo);
  const uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

  fzl::PinnedVmo pinned_vmo;
  zx_status_t status = pinned_vmo.PinRange(
      buffer.offset & ~PageMask(), pagecount * zx_system_get_page_size(), *vmo, bti_, options);
  if (status != ZX_OK) {
    AML_SDMMC_ERROR("bti-pin failed with error %d", status);
    return zx::error(status);
  }

  aml_sdmmc_desc_t* desc = cur_desc;
  for (uint32_t i = 0; i < pinned_vmo.region_count(); i++) {
    fzl::PinnedVmo::Region region = pinned_vmo.region(i);
    if (i == 0) {
      region.phys_addr += buffer.offset & PageMask();
      region.size -= buffer.offset & PageMask();
    }
    if (i == pinned_vmo.region_count() - 1) {
      const size_t end_offset =
          (pagecount * zx_system_get_page_size()) - buffer.size - (buffer.offset & PageMask());
      region.size -= end_offset;
    }

    zx::status<aml_sdmmc_desc_t*> next_desc = PopulateDescriptors(req, desc, region);
    if (next_desc.is_error()) {
      return zx::error(next_desc.error_value());
    }
    desc = next_desc.value();
  }

  return zx::ok(std::pair{desc, std::move(pinned_vmo)});
}

zx::status<aml_sdmmc_desc_t*> AmlSdmmc::PopulateDescriptors(const sdmmc_req_new_t& req,
                                                            aml_sdmmc_desc_t* const cur_desc,
                                                            fzl::PinnedVmo::Region region) {
  if (region.phys_addr > UINT32_MAX || (region.phys_addr + region.size) > UINT32_MAX) {
    AML_SDMMC_ERROR("DMA goes out of accessible range: 0x%0zx, %zu", region.phys_addr, region.size);
    return zx::error(ZX_ERR_BAD_STATE);
  }

  const bool use_block_mode = (1 << log2_ceil(req.blocksize)) == req.blocksize;
  const aml_sdmmc_desc_t* const descs_end =
      descs() + (descs_buffer_.size() / sizeof(aml_sdmmc_desc_t));

  const size_t max_desc_size =
      use_block_mode ? req.blocksize * AmlSdmmcCmdCfg::kMaxBlockCount : req.blocksize;

  aml_sdmmc_desc_t* desc = cur_desc;
  while (region.size > 0) {
    const size_t desc_size = std::min(region.size, max_desc_size);

    if (desc >= descs_end) {
      AML_SDMMC_ERROR("request with more than %d chunks is unsupported\n", AML_DMA_DESC_MAX_COUNT);
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    if (region.phys_addr % AmlSdmmcCmdCfg::kDataAddrAlignment != 0) {
      // The last two bits must be zero to indicate DDR/big-endian.
      AML_SDMMC_ERROR("DMA start address must be 4-byte aligned");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    if (desc_size % req.blocksize != 0) {
      AML_SDMMC_ERROR("DMA length %zu is not multiple of block size %u", desc_size, req.blocksize);
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }

    auto cmd = AmlSdmmcCmdCfg::Get().FromValue(desc->cmd_info);
    if (desc != descs()) {
      cmd = AmlSdmmcCmdCfg::Get().FromValue(0);
      cmd.set_no_resp(1).set_no_cmd(1);
      desc->cmd_arg = 0;
      desc->resp_addr = 0;
    }

    cmd.set_data_io(1);
    if (!(req.cmd_flags & SDMMC_CMD_READ)) {
      cmd.set_data_wr(1);
    }
    cmd.set_owner(1).set_timeout(AmlSdmmcCmdCfg::kDefaultCmdTimeout).set_error(0);

    const size_t blockcount = desc_size / req.blocksize;
    if (use_block_mode) {
      cmd.set_block_mode(1).set_len(static_cast<uint32_t>(blockcount));
    } else if (blockcount == 1) {
      cmd.set_length(req.blocksize);
    } else {
      AML_SDMMC_ERROR("can't send more than one block of size %u", req.blocksize);
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }

    desc->cmd_info = cmd.reg_value();
    desc->data_addr = static_cast<uint32_t>(region.phys_addr);
    desc++;

    region.phys_addr += desc_size;
    region.size -= desc_size;
  }

  return zx::ok(desc);
}

zx_status_t AmlSdmmc::FinishReq(sdmmc_req_t* req) {
  zx_status_t st = ZX_OK;
  if (req->use_dma && req->pmt != ZX_HANDLE_INVALID) {
    /*
     * Clean the cache one more time after the DMA operation because there
     * might be a possibility of cpu prefetching while the DMA operation is
     * going on.
     */
    uint64_t req_len = req->blockcount * req->blocksize;
    if ((req->cmd_flags & SDMMC_CMD_READ) && req->use_dma) {
      st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset, req_len,
                           nullptr, 0);
      if (st != ZX_OK) {
        AML_SDMMC_ERROR("cache clean failed with error  %d", st);
      }
    }

    st = zx_pmt_unpin(req->pmt);
    if (st != ZX_OK) {
      AML_SDMMC_ERROR("error %d in pmt_unpin", st);
    }
    req->pmt = ZX_HANDLE_INVALID;
  }

  return st;
}

zx_status_t AmlSdmmc::SdmmcRequest(sdmmc_req_t* req) {
  {
    fbl::AutoLock lock(&mtx_);
    if (dead_) {
      return ZX_ERR_CANCELED;
    }

    pending_txn_ = true;
  }

  // Wait for the bus to become idle before issuing the next request. This could be necessary if the
  // card is driving CMD low after a voltage switch.
  WaitForBus();

  zx_status_t status = ZX_OK;

  // stop executing
  AmlSdmmcStart::Get().ReadFrom(&mmio_).set_desc_busy(0).WriteTo(&mmio_);

  aml_sdmmc_desc_t* desc;
  aml_sdmmc_desc_t* last_desc;

  SetupCmdDesc(req, &desc);
  last_desc = desc;
  if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    status = SetupDataDescs(req, desc, &last_desc);
    if (status != ZX_OK) {
      AML_SDMMC_ERROR("Failed to setup data descriptors");

      fbl::AutoLock lock(&mtx_);
      pending_txn_ = false;
      txn_finished_.Signal();

      return status;
    }
  }

  auto cmd_info = AmlSdmmcCmdCfg::Get().FromValue(last_desc->cmd_info);
  cmd_info.set_end_of_chain(1);
  last_desc->cmd_info = cmd_info.reg_value();
  AML_SDMMC_TRACE("SUBMIT req:%p cmd_idx: %d cmd_cfg: 0x%x cmd_dat: 0x%x cmd_arg: 0x%x", req,
                  req->cmd_idx, desc->cmd_info, desc->data_addr, desc->cmd_arg);

  zx_paddr_t desc_phys;

  auto start_reg = AmlSdmmcStart::Get().ReadFrom(&mmio_);
  if (req->use_dma) {
    desc_phys = descs_buffer_.phys();
    descs_buffer_.CacheFlush(0, descs_buffer_.size());
    // Read desc from external DDR
    start_reg.set_desc_int(0);
  } else {
    desc_phys = pinned_mmio_.get_paddr() + AML_SDMMC_SRAM_MEMORY_BASE;
    start_reg.set_desc_int(1);
  }

  ClearStatus();

  start_reg.set_desc_busy(1).set_desc_addr((static_cast<uint32_t>(desc_phys)) >> 2).WriteTo(&mmio_);

  zx_status_t res = WaitForInterrupt(req);
  FinishReq(req);
  req->status = res;

  fbl::AutoLock lock(&mtx_);
  pending_txn_ = false;
  txn_finished_.Signal();

  return res;
}

void AmlSdmmc::WaitForBus() const {
  while (!AmlSdmmcStatus::Get().ReadFrom(&mmio_).cmd_i()) {
    zx::nanosleep(zx::deadline_after(zx::usec(10)));
  }
}

zx_status_t AmlSdmmc::TuningDoTransfer(uint8_t* tuning_res, size_t blk_pattern_size,
                                       uint32_t tuning_cmd_idx) {
  sdmmc_req_t tuning_req = {};
  tuning_req.cmd_idx = tuning_cmd_idx;
  tuning_req.cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS;
  tuning_req.arg = 0;
  tuning_req.blockcount = 1;
  tuning_req.blocksize = static_cast<uint16_t>(blk_pattern_size);
  tuning_req.use_dma = false;
  tuning_req.virt_buffer = tuning_res;
  tuning_req.virt_size = blk_pattern_size;
  tuning_req.suppress_error_messages = true;
  return AmlSdmmc::SdmmcRequest(&tuning_req);
}

bool AmlSdmmc::TuningTestSettings(fbl::Span<const uint8_t> tuning_blk, uint32_t tuning_cmd_idx) {
  zx_status_t status = ZX_OK;
  size_t n;
  for (n = 0; n < AML_SDMMC_TUNING_TEST_ATTEMPTS; n++) {
    uint8_t tuning_res[512] = {0};
    status = TuningDoTransfer(tuning_res, tuning_blk.size(), tuning_cmd_idx);
    if (status != ZX_OK || memcmp(tuning_blk.data(), tuning_res, tuning_blk.size()) != 0) {
      break;
    }
  }
  return (n == AML_SDMMC_TUNING_TEST_ATTEMPTS);
}

template <typename SetParamCallback>
AmlSdmmc::TuneWindow AmlSdmmc::TuneDelayParam(fbl::Span<const uint8_t> tuning_blk,
                                              uint32_t tuning_cmd_idx, uint32_t param_max,
                                              SetParamCallback& set_param) {
  TuneWindow best_window, current_window;
  uint32_t first_size = 0;

  char tuning_results[std::max(AmlSdmmcClock::kMaxClkDiv, AmlSdmmcClock::kMaxDelay) + 2];

  for (uint32_t param = 0; param <= param_max; param++) {
    set_param(param);

    if (TuningTestSettings(tuning_blk, tuning_cmd_idx)) {
      tuning_results[param] = '|';

      current_window.size++;
      if (current_window.start == 0) {
        first_size = current_window.size;
      }
    } else {
      tuning_results[param] = '-';

      if (current_window.size > best_window.size) {
        best_window = current_window;
      }

      current_window = {param + 1, 0};
    }
  }

  tuning_results[param_max + 1] = '\0';

  if (current_window.start == 0) {
    best_window = {0, param_max + 1};
  } else if (current_window.size + first_size > best_window.size) {
    // Combine the last window with the first window.
    best_window = {current_window.start, current_window.size + first_size};
  }

  AML_SDMMC_INFO("Tuning results: %s", tuning_results);

  // We're only interested in the delay line results, but that is the last step so this string will
  // end up correct.
  inspect_.tuning_results.Set(tuning_results);

  return best_window;
}

void AmlSdmmc::SetAdjDelay(uint32_t adj_delay) {
  if (board_config_.version_3) {
    AmlSdmmcAdjust::Get().ReadFrom(&mmio_).set_adj_delay(adj_delay).set_adj_fixed(1).WriteTo(
        &mmio_);
  } else {
    AmlSdmmcAdjustV2::Get().ReadFrom(&mmio_).set_adj_delay(adj_delay).set_adj_fixed(1).WriteTo(
        &mmio_);
  }
}

void AmlSdmmc::SetDelayLines(uint32_t delay) {
  if (board_config_.version_3) {
    AmlSdmmcDelay1::Get()
        .ReadFrom(&mmio_)
        .set_dly_0(delay)
        .set_dly_1(delay)
        .set_dly_2(delay)
        .set_dly_3(delay)
        .set_dly_4(delay)
        .WriteTo(&mmio_);
    AmlSdmmcDelay2::Get()
        .ReadFrom(&mmio_)
        .set_dly_5(delay)
        .set_dly_6(delay)
        .set_dly_7(delay)
        .set_dly_8(delay)
        .set_dly_9(delay)
        .WriteTo(&mmio_);
  } else {
    AmlSdmmcDelayV2::Get()
        .ReadFrom(&mmio_)
        .set_dly_0(delay)
        .set_dly_1(delay)
        .set_dly_2(delay)
        .set_dly_3(delay)
        .set_dly_4(delay)
        .set_dly_5(delay)
        .set_dly_6(delay)
        .set_dly_7(delay)
        .WriteTo(&mmio_);
    AmlSdmmcAdjustV2::Get().ReadFrom(&mmio_).set_dly_8(delay).set_dly_9(delay).WriteTo(&mmio_);
  }
}

uint32_t AmlSdmmc::max_delay() const {
  return board_config_.version_3 ? AmlSdmmcClock::kMaxDelay : AmlSdmmcClock::kMaxDelayV2;
}

zx_status_t AmlSdmmc::SdmmcPerformTuning(uint32_t tuning_cmd_idx) {
  fbl::Span<const uint8_t> tuning_blk;

  uint32_t bw = AmlSdmmcCfg::Get().ReadFrom(&mmio_).bus_width();
  if (bw == AmlSdmmcCfg::kBusWidth4Bit) {
    tuning_blk = fbl::Span<const uint8_t>(aml_sdmmc_tuning_blk_pattern_4bit,
                                          sizeof(aml_sdmmc_tuning_blk_pattern_4bit));
  } else if (bw == AmlSdmmcCfg::kBusWidth8Bit) {
    tuning_blk = fbl::Span<const uint8_t>(aml_sdmmc_tuning_blk_pattern_8bit,
                                          sizeof(aml_sdmmc_tuning_blk_pattern_8bit));
  } else {
    AML_SDMMC_ERROR("Tuning at wrong buswidth: %d", bw);
    return ZX_ERR_INTERNAL;
  }

  auto clk = AmlSdmmcClock::Get().ReadFrom(&mmio_);

  auto set_adj_delay = [&](uint32_t param) -> void { SetAdjDelay(param); };
  auto set_delay_lines = [&](uint32_t param) -> void { SetDelayLines(param); };

  set_delay_lines(0);

  TuneWindow phase_windows[AmlSdmmcClock::kMaxClkPhase + 1] = {};
  for (uint32_t phase = 0; phase < std::size(phase_windows); phase++) {
    if (phase != clk.cfg_co_phase()) {
      clk.set_cfg_tx_phase(phase).WriteTo(&mmio_);
      phase_windows[phase] =
          TuneDelayParam(tuning_blk, tuning_cmd_idx, clk.cfg_div() - 1, set_adj_delay);
    }
  }

  TuneWindow adj_delay_window;
  uint32_t best_phase = 0;

  // Find the largest window of working settings.
  for (uint32_t phase = 0; phase < std::size(phase_windows); phase++) {
    if (phase_windows[phase].size > adj_delay_window.size) {
      adj_delay_window = phase_windows[phase];
      best_phase = phase;
    }
  }

  if (adj_delay_window.size == 0) {
    AML_SDMMC_ERROR("No window found for any phase");
    inspect_.tuning_results.Set("failed");
    return ZX_ERR_IO;
  }

  const uint32_t best_adj_delay =
      adj_delay_window.size == clk.cfg_div() ? 0 : adj_delay_window.middle() % clk.cfg_div();

  clk.set_cfg_tx_phase(best_phase).WriteTo(&mmio_);
  inspect_.tx_clock_phase.Set(best_phase);

  set_adj_delay(best_adj_delay);
  inspect_.adj_delay.Set(best_adj_delay);

  TuneWindow delay_window =
      TuneDelayParam(tuning_blk, tuning_cmd_idx, max_delay(), set_delay_lines);

  if (delay_window.size == 0) {
    AML_SDMMC_ERROR("No delay window found");
    return ZX_ERR_IO;
  }

  inspect_.delay_window_size.Set(delay_window.size);

  const uint32_t best_delay = delay_window.middle() % (max_delay() + 1);
  set_delay_lines(best_delay);
  inspect_.delay_lines.Set(best_delay);

  AML_SDMMC_INFO("Clock divider %u, clock phase %u, adj delay %u, delay %u", clk.cfg_div(),
                 best_phase, best_adj_delay, best_delay);

  return ZX_OK;
}

zx_status_t AmlSdmmc::SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo,
                                       uint64_t offset, uint64_t size, uint32_t vmo_rights) {
  if (client_id >= countof(registered_vmos_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (vmo_rights == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  vmo_store::StoredVmo<OwnedVmoInfo> stored_vmo(std::move(vmo), OwnedVmoInfo{
                                                                    .offset = offset,
                                                                    .size = size,
                                                                    .rights = vmo_rights,
                                                                });
  const uint32_t read_perm = (vmo_rights & SDMMC_VMO_RIGHT_READ) ? ZX_BTI_PERM_READ : 0;
  const uint32_t write_perm = (vmo_rights & SDMMC_VMO_RIGHT_WRITE) ? ZX_BTI_PERM_WRITE : 0;
  zx_status_t status = stored_vmo.Pin(bti_, read_perm | write_perm, true);
  if (status != ZX_OK) {
    AML_SDMMC_ERROR("Failed to pin VMO %u for client %u: %s", vmo_id, client_id,
                    zx_status_get_string(status));
    return status;
  }

  return registered_vmos_[client_id]->RegisterWithKey(vmo_id, std::move(stored_vmo));
}

zx_status_t AmlSdmmc::SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo) {
  if (client_id >= countof(registered_vmos_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info = registered_vmos_[client_id]->GetVmo(vmo_id);
  if (!vmo_info) {
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t status = vmo_info->vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_vmo);
  if (status != ZX_OK) {
    return status;
  }

  return registered_vmos_[client_id]->Unregister(vmo_id).status_value();
}

zx_status_t AmlSdmmc::SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) {
  if (req->client_id >= countof(registered_vmos_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  {
    fbl::AutoLock lock(&mtx_);
    if (dead_) {
      return ZX_ERR_CANCELED;
    }

    pending_txn_ = true;
  }

  // Wait for the bus to become idle before issuing the next request. This could be necessary if the
  // card is driving CMD low after a voltage switch.
  WaitForBus();

  // stop executing
  AmlSdmmcStart::Get().ReadFrom(&mmio_).set_desc_busy(0).WriteTo(&mmio_);

  std::optional<std::vector<fzl::PinnedVmo>> pinned_vmos;

  aml_sdmmc_desc_t* desc = SetupCmdDescNew(*req);
  aml_sdmmc_desc_t* last_desc = desc;
  if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    auto status = SetupDataDescsNew(*req, desc);
    if (status.is_error()) {
      AML_SDMMC_ERROR("Failed to setup data descriptors");

      fbl::AutoLock lock(&mtx_);
      pending_txn_ = false;
      txn_finished_.Signal();

      return status.error_value();
    }
    last_desc = std::get<0>(status.value());
    pinned_vmos.emplace(std::move(std::get<1>(status.value())));
  }

  auto cmd_info = AmlSdmmcCmdCfg::Get().FromValue(last_desc->cmd_info);
  cmd_info.set_end_of_chain(1);
  last_desc->cmd_info = cmd_info.reg_value();
  AML_SDMMC_TRACE("SUBMIT req:%p cmd_idx: %d cmd_cfg: 0x%x cmd_dat: 0x%x cmd_arg: 0x%x", req,
                  req->cmd_idx, desc->cmd_info, desc->data_addr, desc->cmd_arg);

  zx_paddr_t desc_phys;

  auto start_reg = AmlSdmmcStart::Get().ReadFrom(&mmio_);
  desc_phys = descs_buffer_.phys();
  descs_buffer_.CacheFlush(0, descs_buffer_.size());
  // Read desc from external DDR
  start_reg.set_desc_int(0);

  ClearStatus();

  start_reg.set_desc_busy(1).set_desc_addr((static_cast<uint32_t>(desc_phys)) >> 2).WriteTo(&mmio_);

  zx::status<std::array<uint32_t, AmlSdmmc::kResponseCount>> response = WaitForInterruptNew(*req);
  if (response.is_error()) {
    return response.error_value();
  }

  memcpy(out_response, response.value().data(), sizeof(uint32_t) * AmlSdmmc::kResponseCount);

  fbl::AutoLock lock(&mtx_);
  pending_txn_ = false;
  txn_finished_.Signal();

  return ZX_OK;
}

zx_status_t AmlSdmmc::Init(const pdev_device_info_t& device_info) {
  // The core clock must be enabled before attempting to access the start register.
  ConfigureDefaultRegs();

  // Stop processing DMA descriptors before releasing quarantine.
  AmlSdmmcStart::Get().ReadFrom(&mmio_).set_desc_busy(0).WriteTo(&mmio_);
  zx_status_t status = bti_.release_quarantine();
  if (status != ZX_OK) {
    AML_SDMMC_ERROR("Failed to release quarantined pages");
    return status;
  }

  dev_info_.caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_SDR104 |
                   SDMMC_HOST_CAP_SDR50 | SDMMC_HOST_CAP_DDR50;
  if (board_config_.supports_dma) {
    dev_info_.caps |= SDMMC_HOST_CAP_DMA;
    status = descs_buffer_.Init(bti_.get(), AML_DMA_DESC_MAX_COUNT * sizeof(aml_sdmmc_desc_t),
                                IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      AML_SDMMC_ERROR("Failed to allocate dma descriptors");
      return status;
    }
    dev_info_.max_transfer_size = AML_DMA_DESC_MAX_COUNT * zx_system_get_page_size();
  } else {
    dev_info_.max_transfer_size = AML_SDMMC_MAX_PIO_DATA_SIZE;
  }

  dev_info_.max_transfer_size_non_dma = AML_SDMMC_MAX_PIO_DATA_SIZE;
  max_freq_ = board_config_.max_freq;
  min_freq_ = board_config_.min_freq;

  inspect_.Init(device_info);
  inspect_.max_delay.Set(max_delay() + 1);

  return ZX_OK;
}

zx_status_t AmlSdmmc::Bind() {
  // Note: This name can't be changed without migrating users in other repos.
  zx_status_t status = DdkAdd(ddk::DeviceAddArgs("aml-sd-emmc").set_inspect_vmo(GetInspectVmo()));
  if (status != ZX_OK) {
    irq_.destroy();
    AML_SDMMC_ERROR("DdkAdd failed");
  }
  return status;
}

zx_status_t AmlSdmmc::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status = ZX_OK;
  auto pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    AML_SDMMC_ERROR("Could not get pdev: %d", status);
    return ZX_ERR_NO_RESOURCES;
  }

  zx::bti bti;
  if ((status = pdev.GetBti(0, &bti)) != ZX_OK) {
    AML_SDMMC_ERROR("Failed to get BTI: %d", status);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    AML_SDMMC_ERROR("Failed to get mmio: %d", status);
    return status;
  }

  // Pin the mmio
  std::optional<ddk::MmioPinnedBuffer> pinned_mmio;
  status = mmio->Pin(bti, &pinned_mmio);
  if (status != ZX_OK) {
    AML_SDMMC_ERROR("Failed to pin mmio: %d", status);
    return status;
  }

  // Populate board specific information
  aml_sdmmc_config_t config;
  size_t actual;
  status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &config, sizeof(config), &actual);
  if (status != ZX_OK || actual != sizeof(config)) {
    AML_SDMMC_ERROR("Failed to get metadata: %d", status);
    return status;
  }

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    AML_SDMMC_ERROR("Failed to get interrupt: %d", status);
    return status;
  }

  pdev_device_info_t dev_info;
  if ((status = pdev.GetDeviceInfo(&dev_info)) != ZX_OK) {
    AML_SDMMC_ERROR("Failed to get device info: %d", status);
    return status;
  }

  // Optional protocol.
  ddk::GpioProtocolClient reset_gpio(parent, "gpio-wifi-power-on");
  if (!reset_gpio.is_valid()) {
    // Alternative name.
    reset_gpio = ddk::GpioProtocolClient(parent, "gpio");
  }

  auto dev =
      std::make_unique<AmlSdmmc>(parent, std::move(bti), *std::move(mmio), *std::move(pinned_mmio),
                                 config, std::move(irq), reset_gpio);

  if ((status = dev->Init(dev_info)) != ZX_OK) {
    return status;
  }

  if ((status = dev->Bind()) != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

void AmlSdmmc::ShutDown() {
  // If there's a pending request, wait for it to complete (and any pages to be unpinned) before
  // proceeding with suspend/unbind.
  {
    fbl::AutoLock lock(&mtx_);
    dead_ = true;

    if (pending_txn_) {
      AML_SDMMC_ERROR("A request was pending after suspend/release");
    }

    while (pending_txn_) {
      txn_finished_.Wait(&mtx_);
    }
  }
}

void AmlSdmmc::DdkSuspend(ddk::SuspendTxn txn) {
  ShutDown();

  // DdkRelease() is not always called after this, so manually unpin the DMA buffers.
  pinned_mmio_.reset();
  descs_buffer_.release();

  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlSdmmc::DdkRelease() {
  ShutDown();
  irq_.destroy();
  delete this;
}

static constexpr zx_driver_ops_t aml_sdmmc_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = AmlSdmmc::Create;
  return driver_ops;
}();

}  // namespace sdmmc

ZIRCON_DRIVER(aml_sdmmc, sdmmc::aml_sdmmc_driver_ops, "zircon", "0.1");
