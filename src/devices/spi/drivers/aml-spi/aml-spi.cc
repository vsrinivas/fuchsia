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

#include <fbl/alloc_checker.h>

#include "registers.h"
#include "src/devices/spi/drivers/aml-spi/aml_spi_bind.h"

namespace spi {

constexpr size_t kBurstMaxBytes = 16;
constexpr size_t kBurstMaxDoubleWords = 16;

constexpr size_t kReset6RegisterOffset = 0x1c;
constexpr uint32_t kSpi0ResetMask = 1 << 1;
constexpr uint32_t kSpi1ResetMask = 1 << 6;

void AmlSpi::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

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

zx::status<fbl::Span<uint8_t>> AmlSpi::GetVmoSpan(uint32_t chip_select, uint32_t vmo_id,
                                                  uint64_t offset, uint64_t size, uint32_t right) {
  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info =
      chips_[chip_select].registered_vmos.GetVmo(vmo_id);
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
  auto conreg = ConReg::Get()
                    .FromValue(0)
                    .set_en(1)
                    .set_mode(ConReg::kModeMaster)
                    .set_bits_per_word(CHAR_BIT - 1)
                    .WriteTo(&mmio_);

  while (size > 0) {
    // Burst size in words (with one byte per word).
    const uint32_t burst_size = std::min(kBurstMaxBytes, size);

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
    auto statreg = StatReg::Get().FromValue(0).set_tc(1).WriteTo(&mmio_);
    conreg.set_burst_length(burst_size - 1).set_xch(1).WriteTo(&mmio_);

    // wait for completion
    do {
      statreg.ReadFrom(&mmio_);
    } while (!statreg.tc());

    // read
    if (out_rxdata) {
      for (uint32_t i = 0; i < burst_size; i++) {
        out_rxdata[i] = static_cast<uint8_t>(mmio_.Read32(AML_SPI_RXDATA));
      }
      out_rxdata += burst_size;
    } else {
      for (uint32_t i = 0; i < burst_size; i++) {
        mmio_.Read32(AML_SPI_RXDATA);
      }
    }

    size -= burst_size;
  }
}

void AmlSpi::Exchange64(const uint8_t* txdata, uint8_t* out_rxdata, size_t size) {
  constexpr size_t kBytesPerWord = sizeof(uint64_t);
  constexpr size_t kMaxBytesPerBurst = kBytesPerWord * kBurstMaxDoubleWords;

  auto conreg = ConReg::Get()
                    .FromValue(0)
                    .set_en(1)
                    .set_mode(ConReg::kModeMaster)
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

    auto statreg = StatReg::Get().FromValue(0).set_tc(1).WriteTo(&mmio_);
    conreg.set_burst_length(burst_size_words - 1).set_xch(1).WriteTo(&mmio_);

    do {
      statreg.ReadFrom(&mmio_);
    } while (!statreg.tc());

    if (out_rxdata) {
      uint64_t* const rx = reinterpret_cast<uint64_t*>(out_rxdata);
      for (uint32_t i = 0; i < burst_size_words; i++) {
        uint64_t value = mmio_.Read32(AML_SPI_RXDATA);
        value = (value << 32) | mmio_.Read32(AML_SPI_RXDATA);
        value = be64toh(value);
        memcpy(&rx[i], &value, sizeof(value));
      }
      out_rxdata += burst_size_words * kBytesPerWord;
    } else {
      for (uint32_t i = 0; i < burst_size_words; i++) {
        mmio_.Read32(AML_SPI_RXDATA);
        mmio_.Read32(AML_SPI_RXDATA);
      }
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

zx_status_t AmlSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                    uint8_t* out_rxdata, size_t rxdata_size,
                                    size_t* out_rxdata_actual) {
  if (cs >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (txdata_size && rxdata_size && (txdata_size != rxdata_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  SetThreadProfile();

  const size_t exchange_size = txdata_size ? txdata_size : rxdata_size;

  // There seems to be a hardware issue where transferring an odd number of bytes corrupts the TX
  // FIFO, but only for subsequent transfers that use 64-bit words. Resetting the IP avoids the
  // problem.
  if (need_reset_ && reset_ && exchange_size >= sizeof(uint64_t)) {
    reset_->WriteRegister32(kReset6RegisterOffset, reset_mask_, reset_mask_);
    need_reset_ = false;
  } else {
    // reset both fifos
    auto testreg = TestReg::Get().FromValue(0).set_fiforst(3).WriteTo(&mmio_);
    do {
      testreg.ReadFrom(&mmio_);
    } while ((testreg.rxcnt() != 0) || (testreg.txcnt() != 0));

    // Resetting seems to leave an extra word in the RX FIFO, so do an extra read just in case.
    mmio_.Read32(AML_SPI_RXDATA);
    mmio_.Read32(AML_SPI_RXDATA);
  }

  chips_[cs].gpio.Write(0);

  // Only use 64-bit words if we will be able to reset the controller.
  if (reset_) {
    Exchange64(txdata, out_rxdata, exchange_size);
  } else {
    Exchange8(txdata, out_rxdata, exchange_size);
  }

  chips_[cs].gpio.Write(1);

  if (out_rxdata && out_rxdata_actual) {
    *out_rxdata_actual = rxdata_size;
  }

  if (exchange_size % 2 == 1) {
    need_reset_ = true;
  }

  return ZX_OK;
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

  return chips_[chip_select].registered_vmos.RegisterWithKey(vmo_id, std::move(stored_vmo));
}

zx_status_t AmlSpi::SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  vmo_store::StoredVmo<OwnedVmoInfo>* const vmo_info =
      chips_[chip_select].registered_vmos.GetVmo(vmo_id);
  if (!vmo_info) {
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t status = vmo_info->vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_vmo);
  if (status != ZX_OK) {
    return status;
  }

  auto result = chips_[chip_select].registered_vmos.Unregister(vmo_id);
  if (result.is_error()) {
    return result.status_value();
  }

  *out_vmo = std::move(result.value());
  return ZX_OK;
}

zx_status_t AmlSpi::SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                       uint64_t size) {
  if (chip_select >= SpiImplGetChipSelectCount()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx::status<fbl::Span<const uint8_t>> buffer =
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

  zx::status<fbl::Span<uint8_t>> buffer =
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

  zx::status<fbl::Span<uint8_t>> tx_buffer =
      GetVmoSpan(chip_select, tx_vmo_id, tx_offset, size, SPI_VMO_RIGHT_READ);
  if (tx_buffer.is_error()) {
    return tx_buffer.error_value();
  }

  zx::status<fbl::Span<uint8_t>> rx_buffer =
      GetVmoSpan(chip_select, rx_vmo_id, rx_offset, size, SPI_VMO_RIGHT_WRITE);
  if (rx_buffer.is_error()) {
    return rx_buffer.error_value();
  }

  return SpiImplExchange(chip_select, tx_buffer->data(), size, rx_buffer->data(), size, nullptr);
}

fbl::Array<AmlSpi::ChipInfo> AmlSpi::InitChips(amlspi_config_t* map, zx_device_t* device) {
  fbl::Array<ChipInfo> chips(new ChipInfo[map->cs_count], map->cs_count);
  if (!chips) {
    return chips;
  }

  for (uint32_t i = 0; i < map->cs_count; i++) {
    uint32_t index = map->cs[i];
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

  pdev_device_info_t info;
  zx_status_t status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get device info: %d", status);
    return status;
  }

  size_t actual;
  amlspi_config_t config = {};
  status =
      device_get_metadata(device, DEVICE_METADATA_AMLSPI_CONFIG, &config, sizeof config, &actual);
  if ((status != ZX_OK) || (actual != sizeof config)) {
    zxlogf(ERROR, "Failed to read config metadata");
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map MMIO: %d", status);
    return status;
  }

  std::optional<fidl::WireSyncClient<fuchsia_hardware_registers::Device>> reset_fidl_client;
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
    reset_fidl_client.emplace(std::move(reset_client));
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
  std::unique_ptr<AmlSpi> spi(new (&ac)
                                  AmlSpi(device, *std::move(mmio), std::move(reset_fidl_client),
                                         reset_mask, std::move(chips), std::move(thread_profile)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
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

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlSpi::Create;
  return ops;
}();

}  // namespace spi

ZIRCON_DRIVER(aml_spi, spi::driver_ops, "zircon", "0.1");
