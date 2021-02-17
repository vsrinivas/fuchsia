// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/status.h>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/span.h>
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
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  uint32_t SpiImplGetChipSelectCount() { return static_cast<uint32_t>(chips_.size()); }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

  zx_status_t SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                 uint64_t offset, uint64_t size, uint32_t rights);
  zx_status_t SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo);
  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size);
  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size);
  zx_status_t SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size);

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
    SpiVmoStore registered_vmos;
  };

  AmlSpi(zx_device_t* device, ddk::MmioBuffer mmio, fbl::Array<ChipInfo> chips)
      : DeviceType(device), mmio_(std::move(mmio)), chips_(std::move(chips)) {}

  static fbl::Array<ChipInfo> InitChips(amlspi_cs_map_t* map, zx_device_t* device);
  void DumpState();

  // Checks size against the registered VMO size and returns a Span with offset applied. Returns a
  // Span with data set to nullptr if vmo_id wasn't found. Returns a Span with size set to zero if
  // offset and/or size are invalid.
  zx::status<fbl::Span<uint8_t>> GetVmoSpan(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                            uint64_t size, uint32_t right);

  ddk::MmioBuffer mmio_;
  fbl::Array<ChipInfo> chips_;
};

}  // namespace spi
