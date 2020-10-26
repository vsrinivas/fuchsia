// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-sdmmc.h"

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/vmo-mapper.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/sdio.h>
#include <hw/sdmmc.h>

#include "dma_descriptors.h"

namespace {

constexpr uint32_t kIdentificationModeBusFreq = 400000;
constexpr int kTuningDelayIterations = 4;

constexpr uint8_t kTuningBlockPattern4Bit[64] = {
    0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc, 0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb, 0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c, 0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff, 0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

constexpr uint8_t kTuningBlockPattern8Bit[128] = {
    0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
    0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
    0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
    0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
    0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

// Returns false if all tuning tests failed. Chooses the best window and sets sample and delay to
// the optimal sample edge and delay values.
bool GetBestWindow(const sdmmc::TuneWindow& rising_window, const sdmmc::TuneWindow& falling_window,
                   uint32_t* sample, uint32_t* delay) {
  uint32_t rising_value = 0;
  uint32_t falling_value = 0;
  uint32_t rising_size = rising_window.GetDelay(&rising_value);
  uint32_t falling_size = falling_window.GetDelay(&falling_value);

  if (rising_size == 0 && falling_size == 0) {
    return false;
  }

  if (falling_size > rising_size) {
    *sample = sdmmc::MsdcIoCon::kSampleFallingEdge;
    *delay = falling_value;
  } else {
    *sample = sdmmc::MsdcIoCon::kSampleRisingEdge;
    *delay = rising_value;
  }

  return true;
}

}  // namespace

namespace sdmmc {

zx_status_t MtkSdmmc::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get composite protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PDev pdev(composite);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx::bti bti;
  if ((status = pdev.GetBti(0, &bti)) != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_get_bti failed", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev.MapMmio failed", __FILE__);
    return status;
  }

  board_mt8167::MtkSdmmcConfig config;
  size_t actual;
  status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &config, sizeof(config), &actual);
  if (status != ZX_OK || actual != sizeof(config)) {
    zxlogf(ERROR, "%s: DdkGetMetadata failed", __FILE__);
    return status;
  }

  sdmmc_host_info_t info = {
      // TODO(fxbug.dev/34596): Re-enable SDR104 once it works without causing CRC errors.
      .caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_AUTO_CMD12 | SDMMC_HOST_CAP_DMA |
              /*SDMMC_HOST_CAP_SDR104 |*/ SDMMC_HOST_CAP_SDR50 | SDMMC_HOST_CAP_DDR50,
      // Assuming 512 is smallest block size we are likely to see.
      .max_transfer_size = SDMMC_PAGES_COUNT * 512,
      .max_transfer_size_non_dma = config.fifo_depth,
      // The datasheet claims that MSDC0 supports EMMC4.5 (and HS400), however there does not
      // appear to be a data strobe input pin on the chip.
      // TODO(bradenkell): Re-enable HS200 after fixing the paving/stability issues.
      .prefs = SDMMC_HOST_PREFS_DISABLE_HS400 | SDMMC_HOST_PREFS_DISABLE_HS200};

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map interrupt", __FILE__);
    return status;
  }

  pdev_device_info_t dev_info;
  if ((status = pdev.GetDeviceInfo(&dev_info)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get device info", __FILE__);
    return status;
  }

  // Both of these fragments are optional.
  ddk::GpioProtocolClient reset_gpio(composite, "gpio-reset");
  ddk::GpioProtocolClient power_en_gpio(composite, "gpio-power-enable");

  fbl::AllocChecker ac;
  std::unique_ptr<MtkSdmmc> device(new (&ac)
                                       MtkSdmmc(parent, *std::move(mmio), std::move(bti), info,
                                                std::move(irq), reset_gpio, power_en_gpio, config));

  if (!ac.check()) {
    zxlogf(ERROR, "%s: MtkSdmmc alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->Bind()) != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

void MtkSdmmc::DdkRelease() {
  irq_.reset();
  JoinIrqThread();
  delete this;
}

zx_status_t MtkSdmmc::Bind() {
  zx_status_t status = DdkAdd("mtk-sdmmc");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __FILE__);
  }

  return status;
}

zx_status_t MtkSdmmc::Init() {
  // Set the clock mode to single data rate; if not starting from POR it could be anything. The
  // clock mode must be set before calling SdmmcSetBusFreq as it is used when calculating the
  // divider.
  SdmmcSetTiming(SDMMC_TIMING_LEGACY);

  // Set bus clock to f_OD (400 kHZ) for identification mode.
  SdmmcSetBusFreq(kIdentificationModeBusFreq);

  auto sdc_cfg = SdcCfg::Get().ReadFrom(&mmio_);
  if (config_.is_sdio) {
    sdc_cfg.set_sdio_interrupt_enable(1).set_sdio_enable(1);
    MsdcIntEn::Get().FromValue(0).set_sdio_irq_enable(1).WriteTo(&mmio_);
  }

  sdc_cfg.set_bus_width(SdcCfg::kBusWidth1).WriteTo(&mmio_);

  DmaCtrl::Get().ReadFrom(&mmio_).set_last_buffer(1).WriteTo(&mmio_);

  // Initialize the io_buffer_t's so they can safely be passed to io_buffer_release().
  gpdma_buf_.vmo_handle = ZX_HANDLE_INVALID;
  gpdma_buf_.pmt_handle = ZX_HANDLE_INVALID;
  gpdma_buf_.phys_list = nullptr;

  bdma_buf_.vmo_handle = ZX_HANDLE_INVALID;
  bdma_buf_.pmt_handle = ZX_HANDLE_INVALID;
  bdma_buf_.phys_list = nullptr;

  auto cb = [](void* arg) -> int { return reinterpret_cast<MtkSdmmc*>(arg)->IrqThread(); };
  if (thrd_create_with_name(&irq_thread_, cb, this, "mt8167-emmc-thread") != thrd_success) {
    zxlogf(ERROR, "%s: Failed to create IRQ thread", __FILE__);
    return ZX_ERR_INTERNAL;
  }

  if (power_en_gpio_.is_valid()) {
    zx_status_t status = power_en_gpio_.ConfigOut(1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set power enable GPIO", __FILE__);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcHostInfo(sdmmc_host_info_t* info) {
  memcpy(info, &info_, sizeof(info_));
  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) { return ZX_OK; }

zx_status_t MtkSdmmc::SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
  uint32_t bus_width_value;

  switch (bus_width) {
    case SDMMC_BUS_WIDTH_MAX:
    case SDMMC_BUS_WIDTH_EIGHT:
      bus_width_value = SdcCfg::kBusWidth8;
      break;
    case SDMMC_BUS_WIDTH_FOUR:
      bus_width_value = SdcCfg::kBusWidth4;
      break;
    case SDMMC_BUS_WIDTH_ONE:
    default:
      bus_width_value = SdcCfg::kBusWidth1;
      break;
  }

  SdcCfg::Get().ReadFrom(&mmio_).set_bus_width(bus_width_value).WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetBusFreq(uint32_t bus_freq) {
  if (bus_freq == 0) {
    MsdcCfg::Get().ReadFrom(&mmio_).set_ck_pwr_down(0).set_ck_drive(0).WriteTo(&mmio_);
    return ZX_OK;
  }

  // For kCardCkModeDiv the bus clock frequency is determined as follows:
  //     msdc_ck = card_ck_div=0: msdc_src_ck / 2
  //               card_ck_div>0: msdc_src_ck / (4 * card_ck_div)
  // For kCardCkModeNoDiv the bus clock frequency is msdc_src_ck
  // For kCardCkModeDdr the bus clock frequency half that of kCardCkModeDiv.
  // For kCardCkModeHs400 the bus clock frequency is the same as kCardCkModeDiv, unless
  // hs400_ck_mode is set in which case it is the same as kCardCkModeNoDiv.

  auto msdc_cfg = MsdcCfg::Get().ReadFrom(&mmio_);

  uint32_t ck_mode = msdc_cfg.card_ck_mode();
  const bool is_ddr = (ck_mode == MsdcCfg::kCardCkModeDdr || ck_mode == MsdcCfg::kCardCkModeHs400);

  uint32_t hs400_ck_mode = msdc_cfg.hs400_ck_mode();

  // Double the requested frequency if a DDR mode is currently selected.
  uint32_t requested = is_ddr ? bus_freq * 2 : bus_freq;

  // Round the divider up, i.e. to a lower frequency.
  uint32_t ck_div = (((config_.src_clk_freq / requested) + 3) / 4);
  if (requested >= config_.src_clk_freq / 2) {
    ck_div = 0;
  } else if (ck_div > 0xfff) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  msdc_cfg.set_ck_pwr_down(0).WriteTo(&mmio_);

  if (ck_mode == MsdcCfg::kCardCkModeHs400) {
    hs400_ck_mode = requested >= config_.src_clk_freq ? 1 : 0;
  } else if (!is_ddr) {
    ck_mode =
        requested >= config_.src_clk_freq ? MsdcCfg::kCardCkModeNoDiv : MsdcCfg::kCardCkModeDiv;
  }

  msdc_cfg.set_hs400_ck_mode(hs400_ck_mode)
      .set_card_ck_mode(ck_mode)
      .set_card_ck_div(ck_div)
      .WriteTo(&mmio_);

  while (!msdc_cfg.ReadFrom(&mmio_).card_ck_stable()) {
  }
  msdc_cfg.set_ck_pwr_down(1).set_ck_drive(1).WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
  uint32_t ck_mode;

  MsdcCfg::Get().ReadFrom(&mmio_).set_ck_pwr_down(0).WriteTo(&mmio_);

  switch (timing) {
    case SDMMC_TIMING_DDR50:
    case SDMMC_TIMING_HSDDR:
      ck_mode = MsdcCfg::kCardCkModeDdr;
      break;
    case SDMMC_TIMING_HS400:
      ck_mode = MsdcCfg::kCardCkModeHs400;
      break;
    case SDMMC_TIMING_SDR104:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      ck_mode = MsdcCfg::kCardCkModeDiv;
      break;
  }

  MsdcCfg::Get().ReadFrom(&mmio_).set_card_ck_mode(ck_mode).WriteTo(&mmio_);
  while (!MsdcCfg::Get().ReadFrom(&mmio_).card_ck_stable()) {
  }
  MsdcCfg::Get().ReadFrom(&mmio_).set_ck_pwr_down(1).WriteTo(&mmio_);

  return ZX_OK;
}

void MtkSdmmc::SdmmcHwReset() {
  MsdcCfg::Get().ReadFrom(&mmio_).set_reset(1).WriteTo(&mmio_);
  while (MsdcCfg::Get().ReadFrom(&mmio_).reset()) {
  }

  if (power_en_gpio_.is_valid()) {
    power_en_gpio_.ConfigOut(0);
  }
  if (reset_gpio_.is_valid()) {
    reset_gpio_.ConfigOut(0);
  }
  if (power_en_gpio_.is_valid()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
    power_en_gpio_.ConfigOut(1);
  }
  if (reset_gpio_.is_valid()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
    reset_gpio_.ConfigOut(1);
  }
}

RequestStatus MtkSdmmc::SendTuningBlock(uint32_t cmd_idx, zx_handle_t vmo) {
  uint32_t bus_width = SdcCfg::Get().ReadFrom(&mmio_).bus_width();

  sdmmc_req_t request;
  request.cmd_idx = cmd_idx;
  request.cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS;
  request.arg = 0;
  request.blockcount = 1;
  request.blocksize = bus_width == SdcCfg::kBusWidth4 ? sizeof(kTuningBlockPattern4Bit)
                                                      : sizeof(kTuningBlockPattern8Bit);
  request.use_dma = true;
  request.dma_vmo = vmo;
  request.buf_offset = 0;

  RequestStatus status = SdmmcRequestWithStatus(&request);
  if (status.Get() != ZX_OK) {
    return status;
  }

  const uint8_t* tuning_block_pattern = kTuningBlockPattern8Bit;
  if (bus_width == SdcCfg::kBusWidth4) {
    tuning_block_pattern = kTuningBlockPattern4Bit;
  }

  uint8_t buf[sizeof(kTuningBlockPattern8Bit)];
  if ((status.data_status = zx_vmo_read(vmo, buf, 0, request.blocksize)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read VMO", __FILE__);
    return status;
  }

  status.data_status =
      memcmp(buf, tuning_block_pattern, request.blocksize) == 0 ? ZX_OK : ZX_ERR_IO;
  return status;
}

template <typename DelayCallback, typename RequestCallback>
void MtkSdmmc::TestDelaySettings(DelayCallback&& set_delay, RequestCallback&& do_request,
                                 TuneWindow* window) {
  char results[PadTune0::kDelayMax + 2];

  for (uint32_t delay = 0; delay <= PadTune0::kDelayMax; delay++) {
    std::forward<DelayCallback>(set_delay)(delay);

    for (int i = 0; i < kTuningDelayIterations; i++) {
      if (std::forward<RequestCallback>(do_request)() != ZX_OK) {
        results[delay] = '-';
        window->Fail();
        break;
      } else if (i == kTuningDelayIterations - 1) {
        results[delay] = '|';
        window->Pass();
      }
    }
  }

  results[std::size(results) - 1] = '\0';
  zxlogf(INFO, "%s: Tuning results: %s", __func__, results);
}

zx_status_t MtkSdmmc::SdmmcPerformTuning(uint32_t cmd_idx) {
  uint32_t bus_width = SdcCfg::Get().ReadFrom(&mmio_).bus_width();
  if (bus_width != SdcCfg::kBusWidth4 && bus_width != SdcCfg::kBusWidth8) {
    return ZX_ERR_INTERNAL;
  }

  // Enable the cmd and data delay lines.
  auto pad_tune0 =
      PadTune0::Get().ReadFrom(&mmio_).set_cmd_delay_sel(1).set_data_delay_sel(1).WriteTo(&mmio_);

  auto msdc_iocon = MsdcIoCon::Get().ReadFrom(&mmio_);

  zx::vmo vmo;
  fzl::VmoMapper vmo_mapper;
  zx_status_t status = vmo_mapper.CreateAndMap(sizeof(kTuningBlockPattern8Bit),
                                               ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create and map VMO", __FILE__);
    return status;
  }

  auto set_cmd_delay = [this](uint32_t delay) {
    PadTune0::Get().ReadFrom(&mmio_).set_cmd_delay(delay).WriteTo(&mmio_);
  };

  zx_handle_t vmo_handle = vmo.get();
  auto test_cmd = [this, vmo_handle, cmd_idx]() {
    return SendTuningBlock(cmd_idx, vmo_handle).cmd_status;
  };

  TuneWindow cmd_rising_window, cmd_falling_window;

  // Find the best window when sampling on the clock rising edge.
  msdc_iocon.set_cmd_sample(MsdcIoCon::kSampleRisingEdge).WriteTo(&mmio_);
  TestDelaySettings(set_cmd_delay, test_cmd, &cmd_rising_window);

  // Find the best window when sampling on the clock falling edge.
  msdc_iocon.set_cmd_sample(MsdcIoCon::kSampleFallingEdge).WriteTo(&mmio_);
  TestDelaySettings(set_cmd_delay, test_cmd, &cmd_falling_window);

  uint32_t cmd_sample, cmd_delay;
  if (!GetBestWindow(cmd_rising_window, cmd_falling_window, &cmd_sample, &cmd_delay)) {
    return ZX_ERR_IO;
  }

  // Select the best sampling edge and delay value.
  msdc_iocon.set_cmd_sample(cmd_sample).WriteTo(&mmio_);
  pad_tune0.set_cmd_delay(cmd_delay).WriteTo(&mmio_);

  auto set_data_delay = [this](uint32_t delay) {
    PadTune0::Get().ReadFrom(&mmio_).set_data_delay(delay).WriteTo(&mmio_);
  };

  auto test_data = [this, vmo_handle, cmd_idx]() {
    return SendTuningBlock(cmd_idx, vmo_handle).Get();
  };

  // Repeat this process for the data bus.
  TuneWindow data_rising_window, data_falling_window;

  msdc_iocon.set_data_sample(MsdcIoCon::kSampleRisingEdge).WriteTo(&mmio_);
  TestDelaySettings(set_data_delay, test_data, &data_rising_window);

  msdc_iocon.set_data_sample(MsdcIoCon::kSampleFallingEdge).WriteTo(&mmio_);
  TestDelaySettings(set_data_delay, test_data, &data_falling_window);

  uint32_t data_sample, data_delay;
  if (!GetBestWindow(data_rising_window, data_falling_window, &data_sample, &data_delay)) {
    return ZX_ERR_IO;
  }

  msdc_iocon.set_data_sample(data_sample).WriteTo(&mmio_);
  pad_tune0.set_data_delay(data_delay).WriteTo(&mmio_);
  zxlogf(INFO, "%s: cmd sample %u, cmd delay %u, data sample %u, data delay %u", __func__,
         cmd_sample, cmd_delay, data_sample, data_delay);

  return ZX_OK;
}

zx_status_t MtkSdmmc::SetupDmaDescriptors(phys_iter_buffer_t* phys_iter_buf) {
  const uint64_t bd_size = phys_iter_buf->phys_count * sizeof(BDmaDescriptor);
  zx_status_t status =
      io_buffer_init(&bdma_buf_, bti_.get(), bd_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create BDMA buffer", __FILE__);
    return status;
  }

  auto bdma_buf_ac = fbl::MakeAutoCall([this]() { io_buffer_release(&bdma_buf_); });

  phys_iter_t phys_iter;
  phys_iter_init(&phys_iter, phys_iter_buf, BDmaDescriptor::kMaxBufferSize);

  zx_paddr_t buf_addr;
  uint64_t desc_count = 0;
  for (size_t buf_size = phys_iter_next(&phys_iter, &buf_addr); buf_size != 0; desc_count++) {
    if (desc_count >= phys_iter_buf->phys_count) {
      zxlogf(ERROR, "%s: Page count mismatch", __FILE__);
      return ZX_ERR_INTERNAL;
    }

    BDmaDescriptor desc;
    desc.SetBuffer(buf_addr);
    desc.size = static_cast<uint32_t>(buf_size);

    // Get the next physical region here so we can check if this is the last descriptor.
    buf_size = phys_iter_next(&phys_iter, &buf_addr);

    desc.SetNext(buf_size == 0 ? 0 : bdma_buf_.phys + ((desc_count + 1) * sizeof(desc)));
    desc.info =
        BDmaDescriptorInfo().set_reg_value(desc.info).set_last(buf_size == 0 ? 1 : 0).reg_value();
    desc.SetChecksum();

    status = zx_vmo_write(bdma_buf_.vmo_handle, &desc, desc_count * sizeof(desc), sizeof(desc));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to write to BDMA buffer", __FILE__);
      return status;
    }
  }

  if (desc_count == 0) {
    zxlogf(ERROR, "%s: No pages provided for DMA buffer", __FILE__);
    return ZX_ERR_INTERNAL;
  }

  const uint64_t gp_size = 2 * sizeof(GpDmaDescriptor);
  status = io_buffer_init(&gpdma_buf_, bti_.get(), gp_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create GPDMA buffer", __FILE__);
    return status;
  }

  auto gpdma_buf_ac = fbl::MakeAutoCall([this]() { io_buffer_release(&gpdma_buf_); });

  GpDmaDescriptor gp_desc;
  gp_desc.info = GpDmaDescriptorInfo().set_reg_value(0).set_hwo(1).set_bdp(1).reg_value();
  gp_desc.SetNext(gpdma_buf_.phys + sizeof(gp_desc));
  gp_desc.SetBDmaDesc(bdma_buf_.phys);
  gp_desc.SetChecksum();

  if ((status = zx_vmo_write(gpdma_buf_.vmo_handle, &gp_desc, 0, sizeof(gp_desc))) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write to GPDMA buffer", __FILE__);
    return status;
  }

  GpDmaDescriptor gp_null_desc;
  status = zx_vmo_write(gpdma_buf_.vmo_handle, &gp_null_desc, sizeof(gp_null_desc),
                        sizeof(gp_null_desc));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write to GPDMA buffer", __FILE__);
    return status;
  }

  if ((status = io_buffer_cache_op(&bdma_buf_, ZX_VMO_OP_CACHE_CLEAN, 0, bd_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: BDMA descriptors cache clean failed", __FILE__);
    return status;
  }

  if ((status = io_buffer_cache_op(&gpdma_buf_, ZX_VMO_OP_CACHE_CLEAN, 0, gp_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: GPDMA descriptors cache clean failed", __FILE__);
    return status;
  }

  bdma_buf_ac.cancel();
  gpdma_buf_ac.cancel();

  return ZX_OK;
}

zx_status_t MtkSdmmc::RequestPrepareDma(sdmmc_req_t* req) {
  const uint64_t req_len = req->blockcount * req->blocksize;
  const bool is_read = req->cmd_flags & SDMMC_CMD_READ;
  const uint64_t pagecount = ((req->buf_offset & kPageMask) + req_len + kPageMask) / PAGE_SIZE;

  if (pagecount > SDMMC_PAGES_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_paddr_t phys[SDMMC_PAGES_COUNT];
  uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
  zx_status_t status = zx_bti_pin(bti_.get(), options, req->dma_vmo, req->buf_offset & ~kPageMask,
                                  PAGE_SIZE * pagecount, phys, pagecount, &req->pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to pin DMA buffer", __FILE__);
    return status;
  }

  auto pmt_ac = fbl::MakeAutoCall([&req]() { zx_pmt_unpin(req->pmt); });

  if (pagecount > 1) {
    phys_iter_buffer_t phys_iter_buf = {.phys = phys,
                                        .phys_count = pagecount,
                                        .length = req_len,
                                        .vmo_offset = req->buf_offset,
                                        .sg_list = nullptr,
                                        .sg_count = 0};

    if ((status = SetupDmaDescriptors(&phys_iter_buf)) != ZX_OK) {
      return status;
    }

    DmaCtrl::Get().ReadFrom(&mmio_).set_dma_mode(DmaCtrl::kDmaModeDescriptor).WriteTo(&mmio_);
    DmaCfg::Get().ReadFrom(&mmio_).set_checksum_enable(1).WriteTo(&mmio_);
    DmaStartAddr::Get().FromValue(0).set(gpdma_buf_.phys).WriteTo(&mmio_);
    DmaStartAddrHigh4Bits::Get().FromValue(0).set(gpdma_buf_.phys).WriteTo(&mmio_);
  } else {
    DmaCtrl::Get().ReadFrom(&mmio_).set_dma_mode(DmaCtrl::kDmaModeBasic).WriteTo(&mmio_);
    DmaLength::Get().FromValue(static_cast<uint32_t>(req_len)).WriteTo(&mmio_);
    DmaStartAddr::Get().FromValue(0).set(phys[0]).WriteTo(&mmio_);
    DmaStartAddrHigh4Bits::Get().FromValue(0).set(phys[0]).WriteTo(&mmio_);
  }

  if (is_read) {
    status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset,
                             req_len, nullptr, 0);
  } else {
    status =
        zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN, req->buf_offset, req_len, nullptr, 0);
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DMA buffer cache clean failed", __FILE__);
    return status;
  }

  MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(0).WriteTo(&mmio_);

  pmt_ac.cancel();
  return status;
}

zx_status_t MtkSdmmc::RequestFinishDma(sdmmc_req_t* req) {
  DmaCtrl::Get().ReadFrom(&mmio_).set_dma_stop(1).WriteTo(&mmio_);
  while (DmaCfg::Get().ReadFrom(&mmio_).dma_active()) {
  }

  zx_status_t cache_status = ZX_OK;
  if (req->cmd_flags & SDMMC_CMD_READ) {
    const uint64_t req_len = req->blockcount * req->blocksize;
    cache_status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset,
                                   req_len, nullptr, 0);
    if (cache_status != ZX_OK) {
      zxlogf(ERROR, "%s: DMA buffer cache invalidate failed", __FILE__);
    }
  }

  io_buffer_release(&gpdma_buf_);
  io_buffer_release(&bdma_buf_);

  zx_status_t unpin_status = zx_pmt_unpin(req->pmt);
  if (unpin_status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to unpin DMA buffer", __FILE__);
  }

  return cache_status != ZX_OK ? cache_status : unpin_status;
}

zx_status_t MtkSdmmc::RequestPreparePolled(sdmmc_req_t* req) {
  MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(1).WriteTo(&mmio_);

  // Clear the FIFO.
  MsdcFifoCs::Get().ReadFrom(&mmio_).set_fifo_clear(1).WriteTo(&mmio_);
  while (MsdcFifoCs::Get().ReadFrom(&mmio_).fifo_clear()) {
  }

  return ZX_OK;
}

zx_status_t MtkSdmmc::RequestFinishPolled(sdmmc_req_t* req) {
  uint32_t bytes_remaining = req->blockcount * req->blocksize;
  uint8_t* data_ptr = reinterpret_cast<uint8_t*>(req->virt_buffer) + req->buf_offset;

  if (req->cmd_flags & SDMMC_CMD_READ) {
    while (bytes_remaining > 0) {
      uint32_t fifo_count = MsdcFifoCs::Get().ReadFrom(&mmio_).rx_fifo_count();

      for (uint32_t i = 0; i < fifo_count; i++) {
        *data_ptr++ = MsdcRxData::Get().ReadFrom(&mmio_).data();
      }

      bytes_remaining -= fifo_count;
    }
  } else {
    while (MsdcFifoCs::Get().ReadFrom(&mmio_).tx_fifo_count() != 0) {
    }

    for (uint32_t i = 0; i < bytes_remaining; i++) {
      MsdcTxData::Get().FromValue(*data_ptr++).WriteTo(&mmio_);
    }
  }

  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcRequest(sdmmc_req_t* req) { return SdmmcRequestWithStatus(req).Get(); }

RequestStatus MtkSdmmc::SdmmcRequestWithStatus(sdmmc_req_t* req) {
  if ((req->blockcount * req->blocksize) > config_.fifo_depth && !req->use_dma &&
      !(req->cmd_flags & SDMMC_CMD_READ)) {
    // TODO(bradenkell): Implement polled block writes greater than the FIFO size.
    return RequestStatus(ZX_ERR_NOT_SUPPORTED);
  }

  uint32_t is_data_request = req->cmd_flags & SDMMC_RESP_DATA_PRESENT;

  zx_status_t status = ZX_OK;

  {
    fbl::AutoLock mutex_al(&mutex_);

    while (SdcStatus::Get().ReadFrom(&mmio_).busy()) {
    }

    SdcBlockNum::Get().FromValue(req->blockcount < 1 ? 1 : req->blockcount).WriteTo(&mmio_);
    SdcArg::Get().FromValue(req->arg).WriteTo(&mmio_);

    if (is_data_request) {
      status = req->use_dma ? RequestPrepareDma(req) : RequestPreparePolled(req);
      if (status != ZX_OK) {
        return RequestStatus(status);
      }
    }

    req_ = req;

    req->status = ZX_ERR_INTERNAL;
    cmd_status_ = ZX_ERR_INTERNAL;

    MsdcIntEn::Get()
        .FromValue(0)
        .set_cmd_crc_err_enable(1)
        .set_cmd_timeout_enable(1)
        .set_cmd_ready_enable(1)
        .WriteTo(&mmio_);

    SdcCmd::FromRequest(req).WriteTo(&mmio_);
  }

  sync_completion_wait(&req_completion_, ZX_TIME_INFINITE);
  sync_completion_reset(&req_completion_);

  fbl::AutoLock mutex_al(&mutex_);

  if (is_data_request) {
    if (req->use_dma) {
      (req->status == ZX_OK ? req->status : status) = RequestFinishDma(req);
    } else if (cmd_status_ == ZX_OK) {
      req->status = RequestFinishPolled(req);
    }
  }

  RequestStatus req_status(cmd_status_, req->status);
  if (req_status.Get() != ZX_OK) {
    // An error occurred, reset the controller.
    MsdcCfg::Get().ReadFrom(&mmio_).set_reset(1).WriteTo(&mmio_);
    while (MsdcCfg::Get().ReadFrom(&mmio_).reset()) {
    }
  }

  return req_status;
}

zx_status_t MtkSdmmc::SdmmcRegisterInBandInterrupt(
    const in_band_interrupt_protocol_t* interrupt_cb) {
  if (!config_.is_sdio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  interrupt_cb_ = ddk::InBandInterruptProtocolClient(interrupt_cb);
  return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcRegisterVmo(uint32_t vmo_id, zx::vmo vmo, uint64_t offset,
                                       uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSdmmc::SdmmcUnregisterVmo(uint32_t vmo_id, zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSdmmc::SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) {
  return ZX_ERR_NOT_SUPPORTED;
}

bool MtkSdmmc::CmdDone(const MsdcInt& msdc_int) {
  if (req_->cmd_flags & SDMMC_RESP_LEN_136) {
    req_->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
    req_->response[1] = SdcResponse::Get(1).ReadFrom(&mmio_).response();
    req_->response[2] = SdcResponse::Get(2).ReadFrom(&mmio_).response();
    req_->response[3] = SdcResponse::Get(3).ReadFrom(&mmio_).response();
  } else if (req_->cmd_flags & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
    req_->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
  }

  if (req_->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
    if (req_->use_dma) {
      if (msdc_int.data_crc_err()) {
        // During tuning it is possible for a data CRC error to be detected before the DMA
        // transaction has been started.
        req_->status = ZX_ERR_IO_DATA_INTEGRITY;
      } else {
        MsdcIntEn::Get()
            .FromValue(0)
            .set_gpd_checksum_err_enable(1)
            .set_bd_checksum_err_enable(1)
            .set_data_crc_err_enable(1)
            .set_data_timeout_enable(1)
            .set_transfer_complete_enable(1)
            .WriteTo(&mmio_);
        DmaCtrl::Get().ReadFrom(&mmio_).set_dma_start(1).WriteTo(&mmio_);
        return false;
      }
    }
  } else {
    req_->status = ZX_OK;
  }

  return true;
}

int MtkSdmmc::IrqThread() {
  while (1) {
    zx::time timestamp;
    if (WaitForInterrupt(&timestamp) != ZX_OK) {
      zxlogf(ERROR, "%s: IRQ wait failed", __FILE__);
      return thrd_error;
    }

    // Read and clear the interrupt flags.
    auto msdc_int = MsdcInt::Get().ReadFrom(&mmio_).WriteTo(&mmio_);

    fbl::AutoLock mutex_al(&mutex_);

    if (msdc_int.sdio_irq()) {
      if (interrupt_cb_.is_valid()) {
        interrupt_cb_.Callback();
      }

      msdc_int.set_sdio_irq(0);
      if (req_ == nullptr) {
        // The controller sometimes sets transfer_complete after an SDIO interrupt, so clear
        // it here to avoid log spam.
        msdc_int.set_transfer_complete(0);
      }

      if (msdc_int.reg_value() == 0) {
        continue;
      }
    }

    if (req_ == nullptr) {
      zxlogf(ERROR, "%s: Received interrupt with no request, MSDC_INT=%08x", __FILE__,
             msdc_int.reg_value());

      // TODO(bradenkell): Interrupts should only be enabled when req_ is valid. Figure out
      // what could cause this state and how to attempt recovery.
      continue;
    }

    if (msdc_int.cmd_crc_err()) {
      cmd_status_ = req_->status = ZX_ERR_IO_DATA_INTEGRITY;
    } else if (msdc_int.cmd_timeout()) {
      cmd_status_ = req_->status = ZX_ERR_TIMED_OUT;
    } else if (msdc_int.cmd_ready()) {
      cmd_status_ = ZX_OK;
      if (!CmdDone(msdc_int)) {
        continue;
      }
    } else if (msdc_int.gpd_checksum_err() || msdc_int.bd_checksum_err()) {
      req_->status = ZX_ERR_INTERNAL;
    } else if (msdc_int.data_crc_err()) {
      req_->status = ZX_ERR_IO_DATA_INTEGRITY;
    } else if (msdc_int.data_timeout()) {
      req_->status = ZX_ERR_TIMED_OUT;
    } else if (msdc_int.transfer_complete()) {
      req_->status = ZX_OK;
    } else {
      zxlogf(WARNING, "%s: Received unexpected interrupt, MSDC_INT=%08x", __FILE__,
             msdc_int.reg_value());
      continue;
    }

    MsdcIntEn::Get().FromValue(0).set_sdio_irq_enable(config_.is_sdio ? 1 : 0).WriteTo(&mmio_);

    req_ = nullptr;
    sync_completion_signal(&req_completion_);
  }
}

zx_status_t MtkSdmmc::WaitForInterrupt(zx::time* timestamp) { return irq_.wait(timestamp); }

}  // namespace sdmmc

static constexpr zx_driver_ops_t mtk_sdmmc_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdmmc::MtkSdmmc::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(mtk_sdmmc, mtk_sdmmc_driver_ops, "zircon", "0.1", 5)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_MSDC0),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_MSDC1),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_MSDC2), ZIRCON_DRIVER_END(mtk_sdmmc)
