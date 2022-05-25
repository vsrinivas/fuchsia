// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"

#include <endian.h>
#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <string.h>
#include <threads.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "registers.h"
#include "src/devices/spi/drivers/aml-spi/aml_spi_bind.h"

namespace spi {

constexpr size_t kNelsonRadarBurstSize = 23224;

// The TX and RX buffer size to allocate for DMA (only if a BTI is provided). This value is set to
// support the Selina driver on Nelson.
constexpr size_t kDmaBufferSize = fbl::round_up<size_t, size_t>(kNelsonRadarBurstSize, PAGE_SIZE);

constexpr size_t kFifoSizeWords = 16;

constexpr size_t kReset6RegisterOffset = 0x1c;
constexpr uint32_t kSpi0ResetMask = 1 << 1;
constexpr uint32_t kSpi1ResetMask = 1 << 6;

void AmlSpi::DdkRelease() { delete this; }

#define dump_reg(reg) zxlogf(ERROR, "%-21s (+%02x): %08x", #reg, reg, mmio_.Read32(reg))

void AmlSpi::DumpState() {
  // skip registers with side-effects
  // dump_reg(AML_SPI_RXDATA);
  // dump_reg(AML_SPI_TXDATA);
  dump_reg(AML_SPI_CONREG);
  dump_reg(AML_SPI_INTREG);
  dump_reg(AML_SPI_DMAREG);
  dump_reg(AML_SPI_STATREG);
  dump_reg(AML_SPI_PERIODREG);
  dump_reg(AML_SPI_TESTREG);
  dump_reg(AML_SPI_DRADDR);
  dump_reg(AML_SPI_DWADDR);
  dump_reg(AML_SPI_LD_CNTL0);
  dump_reg(AML_SPI_LD_CNTL1);
  dump_reg(AML_SPI_LD_RADDR);
  dump_reg(AML_SPI_LD_WADDR);
  dump_reg(AML_SPI_ENHANCE_CNTL);
  dump_reg(AML_SPI_ENHANCE_CNTL1);
  dump_reg(AML_SPI_ENHANCE_CNTL2);
}

#undef dump_reg

zx::status<cpp20::span<uint8_t>> AmlSpi::GetVmoSpan(uint32_t chip_select, uint32_t vmo_id,
                                                    uint64_t offset, uint64_t size,
                                                    uint32_t right) {
  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info = registered_vmos(chip_select)->GetVmo(vmo_id);
  if (!vmo_info) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  if ((vmo_info->meta().rights & right) == 0) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }

  if (offset + size > vmo_info->meta().size) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  return zx::ok(vmo_info->data().subspan(vmo_info->meta().offset + offset));
}

void AmlSpi::Exchange8(const uint8_t* txdata, uint8_t* out_rxdata, size_t size) {
  // transfer settings
  auto conreg = ConReg::Get().ReadFrom(&mmio_).set_bits_per_word(CHAR_BIT - 1).WriteTo(&mmio_);

  while (size > 0) {
    // Burst size in words (with one byte per word).
    const uint32_t burst_size = std::min(kFifoSizeWords, size);

    // fill fifo
    if (txdata) {
      for (uint32_t i = 0; i < burst_size; i++) {
        mmio_.Write32(txdata[i], AML_SPI_TXDATA);
      }
      txdata += burst_size;
    } else {
      for (uint32_t i = 0; i < burst_size; i++) {
        mmio_.Write32(UINT8_MAX, AML_SPI_TXDATA);
      }
    }

    // start burst
    StatReg::Get().FromValue(0).set_tc(1).WriteTo(&mmio_);
    conreg.set_burst_length(burst_size - 1).set_xch(1).WriteTo(&mmio_);

    WaitForTransferComplete();

    // The RX FIFO may not be full immediately after receiving the transfer complete interrupt.
    // Poll until the FIFO has at least one word that can be read.
    for (uint32_t i = 0; i < burst_size; i++) {
      while (StatReg::Get().ReadFrom(&mmio_).rx_fifo_empty()) {
      }

      const uint8_t data = mmio_.Read32(AML_SPI_RXDATA) & 0xff;
      if (out_rxdata) {
        out_rxdata[i] = data;
      }
    }

    if (out_rxdata) {
      out_rxdata += burst_size;
    }

    size -= burst_size;
  }
}

void AmlSpi::Exchange64(const uint8_t* txdata, uint8_t* out_rxdata, size_t size) {
  constexpr size_t kBytesPerWord = sizeof(uint64_t);
  constexpr size_t kMaxBytesPerBurst = kBytesPerWord * kFifoSizeWords;

  auto conreg = ConReg::Get()
                    .ReadFrom(&mmio_)
                    .set_bits_per_word((kBytesPerWord * CHAR_BIT) - 1)
                    .WriteTo(&mmio_);

  while (size >= kBytesPerWord) {
    // Burst size in 64-bit words.
    const uint32_t burst_size_words = std::min(kMaxBytesPerBurst, size) / kBytesPerWord;

    if (txdata) {
      const uint64_t* tx = reinterpret_cast<const uint64_t*>(txdata);
      for (uint32_t i = 0; i < burst_size_words; i++) {
        uint64_t value;
        memcpy(&value, &tx[i], sizeof(value));
        value = be64toh(value);
        // The controller interprets each FIFO entry as a number when they are actually just
        // bytes. To make sure the bytes come out in the intended order, treat them as big-endian,
        // and convert to little-endian for the controller.
        mmio_.Write32(value >> 32, AML_SPI_TXDATA);
        mmio_.Write32(value & UINT32_MAX, AML_SPI_TXDATA);
      }
      txdata += burst_size_words * kBytesPerWord;
    } else {
      for (uint32_t i = 0; i < burst_size_words; i++) {
        mmio_.Write32(UINT32_MAX, AML_SPI_TXDATA);
        mmio_.Write32(UINT32_MAX, AML_SPI_TXDATA);
      }
    }

    StatReg::Get().FromValue(0).set_tc(1).WriteTo(&mmio_);
    conreg.set_burst_length(burst_size_words - 1).set_xch(1).WriteTo(&mmio_);

    WaitForTransferComplete();

    // Same as Exchange8 -- poll until the FIFO has a word that can be read.
    for (uint32_t i = 0; i < burst_size_words; i++) {
      while (StatReg::Get().ReadFrom(&mmio_).rx_fifo_empty()) {
      }

      uint64_t value = mmio_.Read32(AML_SPI_RXDATA);
      value = (value << 32) | mmio_.Read32(AML_SPI_RXDATA);
      value = be64toh(value);

      if (out_rxdata) {
        memcpy(reinterpret_cast<uint64_t*>(out_rxdata) + i, &value, sizeof(value));
      }
    }

    if (out_rxdata) {
      out_rxdata += burst_size_words * kBytesPerWord;
    }

    size -= burst_size_words * kBytesPerWord;
  }

  Exchange8(txdata, out_rxdata, size);
}

void AmlSpi::SetThreadProfile() {
  if (thread_profile_.is_valid()) {
    // Set profile for bus transaction thread.
    // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
    // coding parameters.

    zx::unowned_thread thread{thrd_get_zx_handle(thrd_current())};
    zx_status_t status = thread->set_profile(thread_profile_, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to apply deadline profile: %s", zx_status_get_string(status));
    }
    thread_profile_.reset();
  }
}

void AmlSpi::WaitForTransferComplete() {
  auto statreg = StatReg::Get().FromValue(0);
  while (!statreg.ReadFrom(&mmio_).tc()) {
    interrupt_.wait(nullptr);
  }

  statreg.WriteTo(&mmio_);
}

void AmlSpi::WaitForDmaTransferComplete() {
  auto statreg = StatReg::Get().FromValue(0);
  while (!statreg.te()) {
    interrupt_.wait(nullptr);
    // Clear the transfer complete bit (all others are read-only).
    statreg.set_reg_value(0).set_tc(1).WriteTo(&mmio_).ReadFrom(&mmio_);
  }

  // Wait for the enable bit in DMAREG to be cleared. The TX FIFO empty interrupt apparently
  // indicates this, however in some cases enable is still set after receiving it. Returning
  // without waiting for enable to be cleared leads to data loss, so just poll after the interrupt
  // to make sure.
  while (DmaReg::Get().ReadFrom(&mmio_).enable()) {
  }
}

void AmlSpi::InitRegisters() {
  ConReg::Get().FromValue(0).WriteTo(&mmio_);

  TestReg::GetFromDefaultValue().set_clk_free_en(1).WriteTo(&mmio_);

  ConReg::Get()
      .ReadFrom(&mmio_)
      .set_data_rate(config_.use_enhanced_clock_mode ? 0 : config_.clock_divider_register_value)
      .set_drctl(0)
      .set_ssctl(0)
      .set_smc(0)
      .set_xch(0)
      .set_mode(ConReg::kModeMaster)
      .WriteTo(&mmio_);

  auto enhance_cntl = EnhanceCntl::Get().FromValue(0);
  if (config_.use_enhanced_clock_mode) {
    enhance_cntl.set_clk_cs_delay_enable(1)
        .set_cs_oen_enhance_enable(1)
        .set_clk_oen_enhance_enable(1)
        .set_mosi_oen_enhance_enable(1)
        .set_spi_clk_select(1)  // Use this register instead of CONREG.
        .set_enhance_clk_div(config_.clock_divider_register_value)
        .set_clk_cs_delay(0);
  }
  enhance_cntl.WriteTo(&mmio_);

  EnhanceCntl1::Get().FromValue(0).WriteTo(&mmio_);

  ConReg::Get().ReadFrom(&mmio_).set_en(1).WriteTo(&mmio_);
}

zx_status_t AmlSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                    uint8_t* out_rxdata, size_t rxdata_size,
                                    size_t* out_rxdata_actual) {
  if (cs >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (txdata_size && rxdata_size && (txdata_size != rxdata_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&bus_lock_);

  SetThreadProfile();

  const size_t exchange_size = txdata_size ? txdata_size : rxdata_size;

  const bool use_dma = UseDma(exchange_size);

  // There seems to be a hardware issue where transferring an odd number of bytes corrupts the TX
  // FIFO, but only for subsequent transfers that use 64-bit words. Resetting the IP avoids the
  // problem. DMA transfers do not seem to be affected.
  if (need_reset_ && reset_ && !use_dma && exchange_size >= sizeof(uint64_t)) {
    auto result = reset_->WriteRegister32(kReset6RegisterOffset, reset_mask_, reset_mask_);
    if (!result.ok() || result->result.is_err()) {
      zxlogf(WARNING, "Failed to reset SPI controller");
    }

    InitRegisters();  // The registers must be reinitialized after resetting the IP.
    need_reset_ = false;
  } else {
    // reset both fifos
    auto testreg = TestReg::GetFromDefaultValue().set_fiforst(3).WriteTo(&mmio_);
    do {
      testreg.ReadFrom(&mmio_);
    } while ((testreg.rxcnt() != 0) || (testreg.txcnt() != 0));

    // Resetting seems to leave an extra word in the RX FIFO, so do an extra read just in case.
    mmio_.Read32(AML_SPI_RXDATA);
    mmio_.Read32(AML_SPI_RXDATA);
  }

  IntReg::Get().FromValue(0).set_tcen(1).WriteTo(&mmio_);

  if (gpio(cs).is_valid()) {
    gpio(cs).Write(0);
  }

  zx_status_t status = ZX_OK;

  if (use_dma) {
    status = ExchangeDma(txdata, out_rxdata, exchange_size);
  } else if (reset_) {
    // Only use 64-bit words if we will be able to reset the controller.
    Exchange64(txdata, out_rxdata, exchange_size);
  } else {
    Exchange8(txdata, out_rxdata, exchange_size);
  }

  IntReg::Get().FromValue(0).WriteTo(&mmio_);

  if (gpio(cs).is_valid()) {
    gpio(cs).Write(1);
  }

  if (out_rxdata && out_rxdata_actual) {
    *out_rxdata_actual = rxdata_size;
  }

  if (exchange_size % 2 == 1) {
    need_reset_ = true;
  }

  return status;
}

zx_status_t AmlSpi::SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                       uint64_t offset, uint64_t size, uint32_t rights) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (rights & ~(SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE)) {
    return ZX_ERR_INVALID_ARGS;
  }

  vmo_store::StoredVmo<OwnedVmoInfo> stored_vmo(std::move(vmo), OwnedVmoInfo{
                                                                    .offset = offset,
                                                                    .size = size,
                                                                    .rights = rights,
                                                                });
  const zx_vm_option_t map_opts = ((rights & SPI_VMO_RIGHT_READ) ? ZX_VM_PERM_READ : 0) |
                                  ((rights & SPI_VMO_RIGHT_WRITE) ? ZX_VM_PERM_WRITE : 0);
  zx_status_t status = stored_vmo.Map(map_opts);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AutoLock lock(&vmo_lock_);
  return registered_vmos(chip_select)->RegisterWithKey(vmo_id, std::move(stored_vmo));
}

zx_status_t AmlSpi::SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&vmo_lock_);

  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info = registered_vmos(chip_select)->GetVmo(vmo_id);
  if (!vmo_info) {
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t status = vmo_info->vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_vmo);
  if (status != ZX_OK) {
    return status;
  }

  auto result = registered_vmos(chip_select)->Unregister(vmo_id);
  if (result.is_error()) {
    return result.status_value();
  }

  *out_vmo = std::move(result.value());
  return ZX_OK;
}

void AmlSpi::SpiImplReleaseRegisteredVmos(uint32_t chip_select) {
  fbl::AutoLock lock(&vmo_lock_);
  registered_vmos(chip_select).emplace(vmo_store::Options{});
}

zx_status_t AmlSpi::SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                       uint64_t size) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&vmo_lock_);

  zx::status<cpp20::span<const uint8_t>> buffer =
      GetVmoSpan(chip_select, vmo_id, offset, size, SPI_VMO_RIGHT_READ);
  if (buffer.is_error()) {
    return buffer.error_value();
  }

  return SpiImplExchange(chip_select, buffer->data(), size, nullptr, 0, nullptr);
}

zx_status_t AmlSpi::SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                      uint64_t size) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&vmo_lock_);

  zx::status<cpp20::span<uint8_t>> buffer =
      GetVmoSpan(chip_select, vmo_id, offset, size, SPI_VMO_RIGHT_WRITE);
  if (buffer.is_error()) {
    return buffer.error_value();
  }

  return SpiImplExchange(chip_select, nullptr, 0, buffer->data(), size, nullptr);
}

zx_status_t AmlSpi::SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                       uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock lock(&vmo_lock_);

  zx::status<cpp20::span<uint8_t>> tx_buffer =
      GetVmoSpan(chip_select, tx_vmo_id, tx_offset, size, SPI_VMO_RIGHT_READ);
  if (tx_buffer.is_error()) {
    return tx_buffer.error_value();
  }

  zx::status<cpp20::span<uint8_t>> rx_buffer =
      GetVmoSpan(chip_select, rx_vmo_id, rx_offset, size, SPI_VMO_RIGHT_WRITE);
  if (rx_buffer.is_error()) {
    return rx_buffer.error_value();
  }

  return SpiImplExchange(chip_select, tx_buffer->data(), size, rx_buffer->data(), size, nullptr);
}

zx_status_t AmlSpi::ExchangeDma(const uint8_t* txdata, uint8_t* out_rxdata, uint64_t size) {
  constexpr size_t kBytesPerWord = sizeof(uint64_t);

  if (txdata) {
    if (config_.client_reverses_dma_transfers) {
      memcpy(tx_buffer_.mapped.start(), txdata, size);
    } else {
      // Copy the TX data into the pinned VMO and reverse the endianness.
      auto* tx_vmo = static_cast<uint64_t*>(tx_buffer_.mapped.start());
      for (size_t offset = 0; offset < size; offset += kBytesPerWord) {
        uint64_t tmp;
        memcpy(&tmp, &txdata[offset], sizeof(tmp));
        *tx_vmo++ = be64toh(tmp);
      }
    }
  } else {
    memset(tx_buffer_.mapped.start(), 0xff, size);
  }

  zx_status_t status = tx_buffer_.vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to clean cache: %s", zx_status_get_string(status));
    return status;
  }

  if (out_rxdata) {
    status = rx_buffer_.vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to clean cache: %s", zx_status_get_string(status));
      return status;
    }
  }

  ConReg::Get().ReadFrom(&mmio_).set_bits_per_word((kBytesPerWord * CHAR_BIT) - 1).WriteTo(&mmio_);

  const fzl::PinnedVmo::Region tx_region = tx_buffer_.pinned.region(0);
  const fzl::PinnedVmo::Region rx_region = rx_buffer_.pinned.region(0);

  mmio_.Write32(tx_region.phys_addr, AML_SPI_DRADDR);
  mmio_.Write32(rx_region.phys_addr, AML_SPI_DWADDR);
  mmio_.Write32(0, AML_SPI_PERIODREG);

  DmaReg::Get().FromValue(0).WriteTo(&mmio_);

  // The SPI controller issues requests to DDR to fill the TX FIFO/drain the RX FIFO. The reference
  // driver uses requests up to the FIFO size (16 words) when that many words are remaining, or 2-8
  // word requests otherwise. 16-word requests didn't seem to work in testing, and only 8-word
  // requests are used by default here for simplicity.
  for (size_t words_remaining = size / kBytesPerWord; words_remaining > 0;) {
    const size_t transfer_size = DoDmaTransfer(words_remaining);

    // Enable the TX FIFO empty interrupt and set the start mode control bit on the first run
    // through the loop.
    if (words_remaining == (size / kBytesPerWord)) {
      IntReg::Get().FromValue(0).set_teen(1).WriteTo(&mmio_);
      ConReg::Get().ReadFrom(&mmio_).set_smc(1).WriteTo(&mmio_);
    }

    WaitForDmaTransferComplete();

    words_remaining -= transfer_size;
  }

  DmaReg::Get().ReadFrom(&mmio_).set_enable(0).WriteTo(&mmio_);
  IntReg::Get().FromValue(0).WriteTo(&mmio_);
  LdCntl0::Get().FromValue(0).WriteTo(&mmio_);
  ConReg::Get().ReadFrom(&mmio_).set_smc(0).WriteTo(&mmio_);

  if (out_rxdata) {
    status = rx_buffer_.vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, size, nullptr, 0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to invalidate cache: %s", zx_status_get_string(status));
      return status;
    }

    if (config_.client_reverses_dma_transfers) {
      memcpy(out_rxdata, rx_buffer_.mapped.start(), size);
    } else {
      const auto* rx_vmo = static_cast<uint64_t*>(rx_buffer_.mapped.start());
      for (size_t offset = 0; offset < size; offset += kBytesPerWord) {
        uint64_t tmp = htobe64(*rx_vmo++);
        memcpy(&out_rxdata[offset], &tmp, sizeof(tmp));
      }
    }
  }

  return ZX_OK;
}

size_t AmlSpi::DoDmaTransfer(size_t words_remaining) {
  // These are the limits used by the reference driver, although request sizes up to the FIFO size
  // should work, and the read/write counters are 16 bits wide.
  constexpr size_t kDefaultRequestSizeWords = 8;
  constexpr size_t kMaxRequestCount = 0xfff;

  // TODO(fxbug.dev/100830): It may be possible to complete the transfer in fewer iterations by
  // using request sizes 2-7 instead of 8, like the reference driver does.
  const size_t request_size =
      words_remaining < kFifoSizeWords ? words_remaining : kDefaultRequestSizeWords;
  const size_t request_count = std::min(words_remaining / request_size, kMaxRequestCount);

  LdCntl0::Get().FromValue(0).set_read_counter_enable(1).set_write_counter_enable(1).WriteTo(
      &mmio_);
  LdCntl1::Get()
      .FromValue(0)
      .set_dma_read_counter(request_count)
      .set_dma_write_counter(request_count)
      .WriteTo(&mmio_);

  DmaReg::Get()
      .FromValue(0)
      .set_enable(1)
      // No explanation for these -- see the reference driver.
      .set_urgent(1)
      .set_txfifo_threshold(kFifoSizeWords + 1 - request_size)
      .set_read_request_burst_size(request_size - 1)
      .set_rxfifo_threshold(request_size - 1)
      .set_write_request_burst_size(request_size - 1)
      .WriteTo(&mmio_);

  return request_size * request_count;
}

bool AmlSpi::UseDma(size_t size) const {
  // TODO(fxbug.dev/100830): Support DMA transfers greater than the pre-allocated buffer size.
  return size % sizeof(uint64_t) == 0 && size <= tx_buffer_.mapped.size() &&
         size <= rx_buffer_.mapped.size();
}

fbl::Array<AmlSpi::ChipInfo> AmlSpi::InitChips(amlogic_spi::amlspi_config_t* config,
                                               zx_device_t* device) {
  fbl::Array<ChipInfo> chips(new ChipInfo[config->cs_count], config->cs_count);
  if (!chips) {
    return chips;
  }

  for (uint32_t i = 0; i < config->cs_count; i++) {
    uint32_t index = config->cs[i];
    if (index == amlogic_spi::amlspi_config_t::kCsClientManaged) {
      continue;
    }

    char fragment_name[32] = {};
    snprintf(fragment_name, 32, "gpio-cs-%d", index);
    chips[i].gpio = ddk::GpioProtocolClient(device, fragment_name);
    if (!chips[i].gpio.is_valid()) {
      zxlogf(ERROR, "Failed to get GPIO fragment %u", i);
      return fbl::Array<ChipInfo>();
    }
  }

  return chips;
}

zx_status_t AmlSpi::Create(void* ctx, zx_device_t* device) {
  auto pdev = ddk::PDev::FromFragment(device);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Failed to get platform device fragment");
    return ZX_ERR_NO_RESOURCES;
  }

  size_t actual;
  amlogic_spi::amlspi_config_t config = {};
  zx_status_t status =
      device_get_metadata(device, DEVICE_METADATA_AMLSPI_CONFIG, &config, sizeof config, &actual);
  if ((status != ZX_OK) || (actual != sizeof config)) {
    zxlogf(ERROR, "Failed to read config metadata");
    return status;
  }

  const uint32_t max_clock_div_reg_value =
      config.use_enhanced_clock_mode ? EnhanceCntl::kEnhanceClkDivMax : ConReg::kDataRateMax;
  if (config.clock_divider_register_value > max_clock_div_reg_value) {
    zxlogf(ERROR, "Metadata clock divider value is too large: %u",
           config.clock_divider_register_value);
    return ZX_ERR_INVALID_ARGS;
  }

  std::optional<fdf::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map MMIO: %d", status);
    return status;
  }

  fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset_fidl_client;
  ddk::RegistersProtocolClient reset(device, "reset");
  if (reset.is_valid()) {
    zx::channel reset_server;
    fidl::ClientEnd<fuchsia_hardware_registers::Device> reset_client;
    status = zx::channel::create(0, &reset_server, &reset_client.channel());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to create reset register channel: %d", status);
      return status;
    }

    reset.Connect(std::move(reset_server));
    reset_fidl_client = fidl::BindSyncClient(std::move(reset_client));
  }

  zx::interrupt interrupt;
  if ((status = pdev.GetInterrupt(0, &interrupt)) != ZX_OK) {
    zxlogf(ERROR, "Failed to get SPI interrupt: %d", status);
    return status;
  }

  zx::bti bti;
  status = pdev.GetBti(0, &bti);  // Supplying a BTI is optional.

  DmaBuffer tx_buffer, rx_buffer;
  if (status == ZX_OK) {
    if ((status = DmaBuffer::Create(bti, kDmaBufferSize, &tx_buffer)) != ZX_OK) {
      return status;
    }
    if ((status = DmaBuffer::Create(bti, kDmaBufferSize, &rx_buffer)) != ZX_OK) {
      return status;
    }
    zxlogf(DEBUG, "Got BTI and contiguous buffers, DMA may be used");
  }

  fbl::Array<ChipInfo> chips = InitChips(&config, device);
  if (!chips) {
    return ZX_ERR_NO_RESOURCES;
  }
  if (chips.size() == 0) {
    return ZX_OK;
  }

  zx::profile thread_profile;
  if (config.capacity && config.period) {
    status = device_get_deadline_profile(device, config.capacity, config.period, config.period,
                                         "aml-spi-thread-profile",
                                         thread_profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to get deadline profile: %s", zx_status_get_string(status));
    }
  }

  const uint32_t reset_mask =
      config.bus_id == 0 ? kSpi0ResetMask : (config.bus_id == 1 ? kSpi1ResetMask : 0);

  fbl::AllocChecker ac;
  std::unique_ptr<AmlSpi> spi(
      new (&ac) AmlSpi(device, *std::move(mmio), std::move(reset_fidl_client), reset_mask,
                       std::move(chips), std::move(thread_profile), std::move(interrupt), config,
                       std::move(bti), std::move(tx_buffer), std::move(rx_buffer)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  {
    fbl::AutoLock lock(&spi->bus_lock_);
    spi->InitRegisters();
  }

  char devname[32];
  sprintf(devname, "aml-spi-%u", config.bus_id);

  status = spi->DdkAdd(devname);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed for SPI%u: %d", config.bus_id, status);
    return status;
  }

  status = spi->DdkAddMetadata(DEVICE_METADATA_PRIVATE, &config.bus_id, sizeof config.bus_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAddMetadata failed for SPI%u: %d", config.bus_id, status);
    return status;
  }

  __UNUSED auto* _ = spi.release();

  return ZX_OK;
}

zx_status_t AmlSpi::DmaBuffer::Create(const zx::bti& bti, size_t size, DmaBuffer* out_dma_buffer) {
  zx_status_t status;
  if ((status = zx::vmo::create_contiguous(bti, size, 0, &out_dma_buffer->vmo)) != ZX_OK) {
    zxlogf(ERROR, "Failed to create DMA VMO: %s", zx_status_get_string(status));
    return status;
  }

  status = out_dma_buffer->pinned.Pin(out_dma_buffer->vmo, bti,
                                      ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to pin DMA VMO: %s", zx_status_get_string(status));
    return status;
  }
  if (out_dma_buffer->pinned.region_count() != 1) {
    zxlogf(ERROR, "Invalid region count for contiguous VMO: %u",
           out_dma_buffer->pinned.region_count());
    return status;
  }

  if ((status = out_dma_buffer->mapped.Map(out_dma_buffer->vmo)) != ZX_OK) {
    zxlogf(ERROR, "Failed to map DMA VMO: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlSpi::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(aml_spi, spi::driver_ops, "zircon", "0.1");
