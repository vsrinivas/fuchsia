// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-gspi.h"

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/syscalls/pci/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include "registers.h"
#include "src/devices/spi/drivers/intel-gspi/intel_gspi_bind.h"

namespace gspi {
namespace {
constexpr size_t kMaxFifoDepth = 64;
constexpr size_t kWordSizeBits = 8;
}  // namespace

zx_status_t GspiDevice::Create(void* ctx, zx_device_t* parent) {
  ddk::Pci pci(parent, "pci");
  std::optional<ddk::MmioBuffer> mmio;
  zx_status_t status = pci.MapMmio(0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "gspi Failed to map mmio: %d\n", status);
    return status;
  }

  auto acpi = acpi::Client::Create(parent);
  if (acpi.is_error()) {
    return acpi.error_value();
  }

  zx::interrupt irq;
  status = pci.ConfigureIrqMode(1, nullptr);
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to ConfigureIrqMode: %s", zx_status_get_string(status));
  } else {
    status = pci.MapInterrupt(0, &irq);
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to map interrupt: %s", zx_status_get_string(status));
    }
  }
  // If there's no interrupt, we'll just poll.

  auto dev = std::make_unique<GspiDevice>(parent, std::move(mmio.value()), std::move(irq),
                                          std::move(acpi.value()));
  return dev->Bind(&dev);
}

zx_status_t GspiDevice::Bind(std::unique_ptr<GspiDevice>* device_ptr) {
  // Turn off the controller.
  Con0Reg::Get().ReadFrom(&mmio_).set_sse(0).WriteTo(&mmio_);
  // We're going to control chip select.
  CSControlReg::Get()
      .ReadFrom(&mmio_)
      .set_cs_mode(CSControlReg::Mode::kChipSelectSW)
      .set_cs_state(1)  // Set CS to high.
      .WriteTo(&mmio_);

  if (irq_.is_valid()) {
    irq_thread_ = std::thread(&GspiDevice::IrqThread, this);
  }

  zx_status_t status =
      DdkAdd(ddk::DeviceAddArgs("intel-gspi").set_inspect_vmo(inspect_.DuplicateVmo()));
  if (status == ZX_OK) {
    __UNUSED auto dev = device_ptr->release();
  }
  return status;
}

void GspiDevice::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;
  auto responder = fit::defer([&]() { txn.Reply(status); });
  auto con1 = Con1Reg::Get().ReadFrom(&mmio_);
  // Disable all interrupts except for TX fifo and RX fifo interrupts.
  con1.set_rwot(0).set_trail(0).set_tsre(0).set_rsre(0).set_tinte(0).set_tie(1).set_rie(1);

  // ValidateChildConfig configures clock and cs polarities.
  // For now all devices must have the same clock and cs configuration.
  status = ValidateChildConfig(con1);
  if (status != ZX_OK) {
    return;
  }
  con1.WriteTo(&mmio_);

  // Set up word size.
  auto con0 = Con0Reg::Get().ReadFrom(&mmio_);
  con0.set_dss(kWordSizeBits - 1).set_edss(0);
  con0.set_ecs(0).set_frf(0);
  con0.WriteTo(&mmio_);

  auto result = acpi_.borrow().GetBusId();
  if (!result.ok()) {
    zxlogf(ERROR, "failed to get bus id: %s", result.FormatDescription().data());
    status = result.status();
    return;
  }
  if (result->result.is_err()) {
    // This probably just means that we don't have any children.
    // This is not an error. We should just put the SPI bus into some kind of
    // low-power state.
    return;
  }

  uint32_t bus_id = result->result.response().bus_id;
  zxlogf(INFO, "using SPI bus ID %u", bus_id);
  status = DdkAddMetadata(DEVICE_METADATA_PRIVATE, &bus_id, sizeof(bus_id));
}

void GspiDevice::DdkUnbind(ddk::UnbindTxn txn) {
  irq_.destroy();
  if (irq_thread_.joinable()) {
    irq_thread_.join();
  }

  txn.Reply();
}

void GspiDevice::DdkRelease() { delete this; }

void GspiDevice::IrqThread() {
  while (true) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(WARNING, "irq wait failed: %d", status);
      return;
    }

    StatusReg reg = StatusReg::Get().ReadFrom(&mmio_);
    // TODO(fxbug.dev/77485): pay attention to errors and deal with them appropriately.
    if (reg.rfs()) {
      sync_completion_signal(&ready_for_rx_);
    }

    if (reg.tfs()) {
      sync_completion_signal(&ready_for_tx_);
    }

    reg.WriteTo(&mmio_);
    irq_count_.Add(1);
  }
}

zx_status_t GspiDevice::WaitForFifoService(bool rx) {
  StatusReg status = StatusReg::Get().FromValue(0);
  while (rx ? !status.ReadFrom(&mmio_).rfs() : !status.ReadFrom(&mmio_).tfs()) {
  }

  status.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t GspiDevice::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                        uint8_t* out_rxdata, size_t rxdata_size,
                                        size_t* out_rxdata_actual) {
  std::scoped_lock lock(lock_);
  if (locked_cs_.has_value() && locked_cs_.value() != cs) {
    return ZX_ERR_UNAVAILABLE;
  }
  if (locked_cs_ == std::nullopt) {
    zx_status_t cs_status = SetChipSelect(cs);
    if (cs_status != ZX_OK) {
      return cs_status;
    }
  }

  // Enable the controller.
  auto con0 = Con0Reg::Get().ReadFrom(&mmio_);
  con0.set_sse(1);
  con0.WriteTo(&mmio_);

  unsigned int sirfl = ReceiveFifoReg::Get().ReadFrom(&mmio_).sirfl();
  if (sirfl) {
    // We don't expect this to happen - but if it happens, we'll just drain the fifo.
    zxlogf(WARNING, "%u entries in receive fifo", sirfl);
  }

  // Drain RX fifo.
  auto fifo = FifoReg::Get();
  auto status = StatusReg::Get().ReadFrom(&mmio_);
  while (status.rne()) {
    fifo.ReadFrom(&mmio_);
    status.ReadFrom(&mmio_);
  }

  // Now that the RX fifo is drained, we clear the tx and rx sync completions.
  sync_completion_reset(&ready_for_rx_);
  sync_completion_reset(&ready_for_tx_);
  size_t transaction_size = txdata_size ? txdata_size : rxdata_size;
  zx_status_t return_value = ZX_OK;
  while (transaction_size > 0) {
    size_t burst = std::min(transaction_size, kMaxFifoDepth);
    // We want to receive an interrupt when the RX fifo has |burst| entries in it.
    ReceiveFifoReg::Get().FromValue(0).set_wmrf(burst - 1).WriteTo(&mmio_);
    // We want to be interrupted when the TX fifo is empty.
    // The docs make no mention of the high watermark, so we just set it to 64.
    TransmitFifoReg::Get().FromValue(0).set_lwmtf(1).set_hwmtf(63).WriteTo(&mmio_);

    // Send as much data as we can.
    for (size_t i = 0; i < burst; i++) {
      uint32_t value = 0;
      if (txdata) {
        value = *txdata;
        txdata++;
      }
      fifo.FromValue(value).WriteTo(&mmio_);
    }

    if (!irq_.is_valid()) {
      return_value = WaitForFifoService(/*rx=*/true);
      if (return_value != ZX_OK) {
        zxlogf(ERROR, "error while waiting for interrupt: %d", return_value);
        // TODO(fxbug.dev/77485): what do we do here? reset the controller?
        break;
      }
    } else {
      return_value = sync_completion_wait(&ready_for_rx_, irq_timeout_.get());
      sync_completion_reset(&ready_for_rx_);
      if (return_value == ZX_ERR_TIMED_OUT) {
        zxlogf(ERROR, "rx interrupt timed out. RX fifo reg: 0x%08x, Status: 0x%08x",
               ReceiveFifoReg::Get().ReadFrom(&mmio_).reg_value(),
               StatusReg::Get().ReadFrom(&mmio_).reg_value());
        break;
      }
      for (size_t i = 0; i < burst; i++) {
        bool warned = false;
        while (!StatusReg::Get().ReadFrom(&mmio_).rne()) {
          // Make sure that the RX fifo actually has data in it.
          // If this happens, it means that ready_for_rx_ was signalled
          // even though we didn't get the RX fifo service IRQ - so we print out a warning.
          if (!warned) {
            zxlogf(ERROR, "RX emptied, status=0x%08x rx fifo=0x%08x burst=%zu read=%zu",
                   StatusReg::Get().ReadFrom(&mmio_).reg_value(),
                   ReceiveFifoReg::Get().ReadFrom(&mmio_).reg_value(), burst, i);
            warned = true;
          }
        }
        uint8_t val = fifo.ReadFrom(&mmio_).data() & ((1 << kWordSizeBits) - 1);
        if (out_rxdata) {
          *out_rxdata = val;
          out_rxdata++;
        }
      }

      if (!irq_.is_valid()) {
        return_value = WaitForFifoService(false);
        if (return_value != ZX_OK) {
          zxlogf(ERROR, "error while waiting for interrupt: %d", return_value);
          break;
        }
      } else {
        return_value = sync_completion_wait(&ready_for_tx_, irq_timeout_.get());
        sync_completion_reset(&ready_for_tx_);
        if (return_value == ZX_ERR_TIMED_OUT) {
          zxlogf(ERROR, "tx interrupt timed out. TX fifo reg: 0x%08x, Status: 0x%08x",
                 TransmitFifoReg::Get().ReadFrom(&mmio_).reg_value(),
                 StatusReg::Get().ReadFrom(&mmio_).reg_value());
          return_value = ZX_ERR_TIMED_OUT;
          break;
        }
      }
      transaction_size -= burst;
    }
  }

  if (locked_cs_ == std::nullopt) {
    DeassertChipSelect();
  }

  // Disable the controller.
  con0 = Con0Reg::Get().ReadFrom(&mmio_);
  con0.set_sse(0);
  con0.WriteTo(&mmio_);

  if (return_value == ZX_OK) {
    *out_rxdata_actual = txdata_size ? txdata_size : rxdata_size;
  }

  return return_value;
}

zx_status_t GspiDevice::SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                           uint64_t offset, uint64_t size, uint32_t rights) {
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t GspiDevice::SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id,
                                             zx::vmo* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t GspiDevice::SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                           uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t GspiDevice::SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                          uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t GspiDevice::SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id,
                                           uint64_t tx_offset, uint32_t rx_vmo_id,
                                           uint64_t rx_offset, uint64_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t GspiDevice::SpiImplLockBus(uint32_t chip_select) {
  std::scoped_lock lock(lock_);
  if (locked_cs_ != std::nullopt) {
    zxlogf(ERROR, "failed to lock for %u: already locked by %u", chip_select, locked_cs_.value());
    return ZX_ERR_UNAVAILABLE;
  }

  zx_status_t status = SetChipSelect(chip_select);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to lock - bad cs");
    return status;
  }
  locked_cs_ = chip_select;
  return ZX_OK;
}

zx_status_t GspiDevice::SpiImplUnlockBus(uint32_t chip_select) {
  std::scoped_lock lock(lock_);
  if (locked_cs_ == std::nullopt) {
    zxlogf(ERROR, "unlock failed: not locked");
    return ZX_ERR_BAD_STATE;
  }

  if (locked_cs_.value() != chip_select) {
    zxlogf(ERROR, "unlock failed: bad cs (%u vs %u)", locked_cs_.value(), chip_select);
    return ZX_ERR_UNAVAILABLE;
  }

  DeassertChipSelect();
  locked_cs_ = std::nullopt;
  return ZX_OK;
}

zx_status_t GspiDevice::SetChipSelect(uint32_t cs) {
  if (cs >= GSPI_CS_COUNT) {
    zxlogf(ERROR, "Invalid chip select %u", cs);
    return ZX_ERR_OUT_OF_RANGE;
  }
  auto reg = CSControlReg::Get().ReadFrom(&mmio_);
  reg.set_cs1_output_sel(cs).set_cs_state(0).WriteTo(&mmio_);
  return ZX_OK;
}

void GspiDevice::DeassertChipSelect() {
  CSControlReg::Get().ReadFrom(&mmio_).set_cs_state(1).WriteTo(&mmio_);
}

zx_status_t GspiDevice::ValidateChildConfig(Con1Reg& con1) {
  size_t metadata_size;
  auto status = device_get_metadata_size(zxdev(), DEVICE_METADATA_SPI_CHANNELS, &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __func__, status);
    return status;
  }

  auto buffer_deleter = std::make_unique<uint8_t[]>(metadata_size);
  auto buffer = buffer_deleter.get();

  size_t actual;
  status =
      device_get_metadata(zxdev(), DEVICE_METADATA_SPI_CHANNELS, buffer, metadata_size, &actual);
  if (status != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return (status == ZX_OK ? ZX_ERR_INVALID_ARGS : status);
  }

  fidl::DecodedMessage<fuchsia_hardware_spi::wire::SpiBusMetadata> decoded(
      fidl::internal::kLLCPPEncodedWireFormatVersion, buffer, metadata_size);
  fuchsia_hardware_spi::wire::SpiBusMetadata* metadata = decoded.PrimaryObject();
  if (!metadata->has_channels()) {
    zxlogf(INFO, "%s: no channels supplied.", __func__);
    return ZX_OK;
  }

  auto& channels = metadata->channels();
  if (channels.count() > GSPI_CS_COUNT) {
    zxlogf(ERROR, "%s: too many SPI children!", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  fuchsia_hardware_spi::wire::SpiClockPhase phase = channels[0].clock_phase();
  bool cs_high = channels[0].cs_polarity_high();
  bool clk_high = channels[0].clock_polarity_high();
  for (auto& chan : channels) {
    // TODO(fxbug.dev/77485): we should configure clocks, and also track each child. That way we
    // could support multiple devices with different configurations on the same bus.
    // For now we just require that everything has the same configuration.
    if (chan.clock_phase() != phase || chan.cs_polarity_high() != cs_high ||
        chan.clock_polarity_high() != clk_high) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  con1.set_sph(phase == fuchsia_hardware_spi::wire::SpiClockPhase::kClockPhaseFirst ? 0 : 1);
  con1.set_spo(clk_high ? 1 : 0);
  con1.set_ifs(cs_high ? 1 : 0);

  auto cs_reg = CSControlReg::Get().ReadFrom(&mmio_);
  cs_reg.set_cs0_polarity(cs_high ? 1 : 0);
  cs_reg.WriteTo(&mmio_);
  return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = GspiDevice::Create;
  return ops;
}();
}  // namespace gspi

// clang-format off
ZIRCON_DRIVER(intel-gspi, gspi::driver_ops, "zircon", "0.1");
