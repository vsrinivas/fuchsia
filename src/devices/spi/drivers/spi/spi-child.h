// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_
#define SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>
#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace spi {

class SpiDevice;

class SpiChild;
using SpiChildType = ddk::Device<SpiChild, ddk::Messageable<fuchsia_hardware_spi::Device>::Mixin,
                                 ddk::Unbindable, ddk::Openable, ddk::Closable>;

class SpiChild : public SpiChildType,
                 public fbl::RefCounted<SpiChild>,
                 public ddk::SpiProtocol<SpiChild, ddk::base_protocol> {
 public:
  SpiChild(zx_device_t* parent, ddk::SpiImplProtocolClient spi, uint32_t chip_select,
           SpiDevice* spi_parent, bool has_siblings)
      : SpiChildType(parent),
        spi_(spi),
        cs_(chip_select),
        spi_parent_(*spi_parent),
        has_siblings_(has_siblings) {}

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);

  void TransmitVector(TransmitVectorRequestView request,
                      TransmitVectorCompleter::Sync& completer) override;
  void ReceiveVector(ReceiveVectorRequestView request,
                     ReceiveVectorCompleter::Sync& completer) override;
  void ExchangeVector(ExchangeVectorRequestView request,
                      ExchangeVectorCompleter::Sync& completer) override;

  void RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) override;
  void UnregisterVmo(UnregisterVmoRequestView request,
                     UnregisterVmoCompleter::Sync& completer) override;

  void Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) override;
  void Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) override;
  void Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) override;

  void CanAssertCs(CanAssertCsRequestView request, CanAssertCsCompleter::Sync& completer) override;
  void AssertCs(AssertCsRequestView request, AssertCsCompleter::Sync& completer) override;
  void DeassertCs(DeassertCsRequestView request, DeassertCsCompleter::Sync& completer) override;

  zx_status_t SpiTransmit(const uint8_t* txdata_list, size_t txdata_count);
  zx_status_t SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                         size_t* out_rxdata_actual);
  zx_status_t SpiExchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list,
                          size_t rxdata_count, size_t* out_rxdata_actual);
  void SpiConnectServer(zx::channel server);

  void OnUnbound();

 private:
  const ddk::SpiImplProtocolClient spi_;
  const uint32_t cs_;
  SpiDevice& spi_parent_;
  // False if this child is the only device on the bus.
  const bool has_siblings_;

  fbl::Mutex lock_;
  bool connected_ TA_GUARDED(lock_) = false;
  bool shutdown_ = false;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_
