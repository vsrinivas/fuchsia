// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/profile.h>
#include <lib/zx/status.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-spi.h>

#include "src/lib/vmo_store/vmo_store.h"

namespace spi {

class AmlSpi;
using DeviceType = ddk::Device<AmlSpi, ddk::Unbindable>;

class AmlSpi : public DeviceType, public ddk::SpiImplProtocol<AmlSpi, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* device);

  // Device protocol implementation.
  void DdkRelease();

  void DdkUnbind(ddk::UnbindTxn txn);

  uint32_t SpiImplGetChipSelectCount() { return static_cast<uint32_t>(chips_.size()); }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

  zx_status_t SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                 uint64_t offset, uint64_t size, uint32_t rights);
  zx_status_t SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo);
  void SpiImplReleaseRegisteredVmos(uint32_t chip_select);
  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size);
  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size);
  zx_status_t SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size);

  zx_status_t SpiImplLockBus(uint32_t chip_select) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SpiImplUnlockBus(uint32_t chip_select) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  struct OwnedVmoInfo {
    uint64_t offset;
    uint64_t size;
    uint32_t rights;
  };

  using SpiVmoStore = vmo_store::VmoStore<vmo_store::HashTableStorage<uint32_t, OwnedVmoInfo>>;

  struct ChipInfo {
    ChipInfo() : registered_vmos(vmo_store::Options{}) {}
    ~ChipInfo() = default;

    ddk::GpioProtocolClient gpio;
    std::optional<SpiVmoStore> registered_vmos;
  };

  // DmaBuffer holds a contiguous VMO that is both pinned and mapped.
  struct DmaBuffer {
    static zx_status_t Create(const zx::bti& bti, size_t size, DmaBuffer* out_dma_buffer);

    zx::vmo vmo;
    fzl::PinnedVmo pinned;
    fzl::VmoMapper mapped;
  };

  AmlSpi(zx_device_t* device, fdf::MmioBuffer mmio,
         fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset, uint32_t reset_mask,
         fbl::Array<ChipInfo> chips, zx::profile thread_profile, zx::interrupt interrupt,
         const amlogic_spi::amlspi_config_t& config, zx::bti bti, DmaBuffer tx_buffer,
         DmaBuffer rx_buffer)
      : DeviceType(device),
        mmio_(std::move(mmio)),
        reset_(std::move(reset)),
        reset_mask_(reset_mask),
        chips_(std::move(chips)),
        thread_profile_(std::move(thread_profile)),
        interrupt_(std::move(interrupt)),
        config_(config),
        bti_(std::move(bti)),
        tx_buffer_(std::move(tx_buffer)),
        rx_buffer_(std::move(rx_buffer)) {}

  static fbl::Array<ChipInfo> InitChips(amlogic_spi::amlspi_config_t* config, zx_device_t* device);
  void DumpState() TA_REQ(bus_lock_);

  void Exchange8(const uint8_t* txdata, uint8_t* out_rxdata, size_t size) TA_REQ(bus_lock_);
  void Exchange64(const uint8_t* txdata, uint8_t* out_rxdata, size_t size) TA_REQ(bus_lock_);

  void SetThreadProfile();

  void WaitForTransferComplete() TA_REQ(bus_lock_);
  void WaitForDmaTransferComplete() TA_REQ(bus_lock_);

  void InitRegisters() TA_REQ(bus_lock_);

  // Checks size against the registered VMO size and returns a Span with offset applied. Returns a
  // Span with data set to nullptr if vmo_id wasn't found. Returns a Span with size set to zero if
  // offset and/or size are invalid.
  zx::status<cpp20::span<uint8_t>> GetVmoSpan(uint32_t chip_select, uint32_t vmo_id,
                                              uint64_t offset, uint64_t size, uint32_t right)
      TA_REQ(vmo_lock_);

  zx_status_t ExchangeDma(const uint8_t* txdata, uint8_t* out_rxdata, uint64_t size)
      TA_REQ(bus_lock_);

  size_t DoDmaTransfer(size_t words_remaining) TA_REQ(bus_lock_);

  bool UseDma(size_t size) const TA_REQ(bus_lock_);

  void Shutdown();

  // Shims to support thread annotations on ChipInfo members.
  const ddk::GpioProtocolClient& gpio(uint32_t chip_select) TA_REQ(bus_lock_) {
    return chips_[chip_select].gpio;
  }

  std::optional<SpiVmoStore>& registered_vmos(uint32_t chip_select) TA_REQ(vmo_lock_) {
    return chips_[chip_select].registered_vmos;
  }

  fdf::MmioBuffer mmio_ TA_GUARDED(bus_lock_);
  fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset_;
  const uint32_t reset_mask_;
  const fbl::Array<ChipInfo> chips_;
  bool need_reset_ TA_GUARDED(bus_lock_) = false;
  zx::profile thread_profile_;
  zx::interrupt interrupt_;
  const amlogic_spi::amlspi_config_t config_;
  // Protects mmio_, need_reset_, and the DMA buffers.
  fbl::Mutex bus_lock_;
  // Protects registered_vmos members of chips_.
  fbl::Mutex vmo_lock_;
  zx::bti bti_;
  DmaBuffer tx_buffer_ TA_GUARDED(bus_lock_);
  DmaBuffer rx_buffer_ TA_GUARDED(bus_lock_);
  bool shutdown_ TA_GUARDED(bus_lock_) = false;
};

}  // namespace spi
