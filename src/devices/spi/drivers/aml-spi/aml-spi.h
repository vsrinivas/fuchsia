// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/profile.h>
#include <lib/zx/status.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <soc/aml-common/aml-spi.h>

#include "src/lib/vmo_store/vmo_store.h"

namespace spi {

class AmlSpi;
using DeviceType = ddk::Device<AmlSpi>;

class AmlSpi : public DeviceType, public ddk::SpiImplProtocol<AmlSpi, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* device);

  // Device protocol implementation.
  void DdkRelease();

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

  AmlSpi(zx_device_t* device, ddk::MmioBuffer mmio,
         fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset, uint32_t reset_mask,
         fbl::Array<ChipInfo> chips, zx::profile thread_profile, zx::interrupt interrupt)
      : DeviceType(device),
        mmio_(std::move(mmio)),
        reset_(std::move(reset)),
        reset_mask_(reset_mask),
        chips_(std::move(chips)),
        thread_profile_(std::move(thread_profile)),
        interrupt_(std::move(interrupt)) {}

  static fbl::Array<ChipInfo> InitChips(amlspi_config_t* config, zx_device_t* device);
  void DumpState();

  void Exchange8(const uint8_t* txdata, uint8_t* out_rxdata, size_t size);
  void Exchange64(const uint8_t* txdata, uint8_t* out_rxdata, size_t size);

  void SetThreadProfile();

  void WaitForTransferComplete();

  // Checks size against the registered VMO size and returns a Span with offset applied. Returns a
  // Span with data set to nullptr if vmo_id wasn't found. Returns a Span with size set to zero if
  // offset and/or size are invalid.
  zx::status<cpp20::span<uint8_t>> GetVmoSpan(uint32_t chip_select, uint32_t vmo_id,
                                              uint64_t offset, uint64_t size, uint32_t right);

  ddk::MmioBuffer mmio_;
  fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset_;
  const uint32_t reset_mask_;
  fbl::Array<ChipInfo> chips_;
  bool need_reset_ = false;
  zx::profile thread_profile_;
  zx::interrupt interrupt_;
};

}  // namespace spi
