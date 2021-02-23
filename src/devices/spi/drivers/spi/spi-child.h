// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_
#define SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_

#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/spi/llcpp/fidl.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>

#include <ddk/metadata/spi.h>
#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace spi {

class SpiDevice;

class SpiChild;
using SpiChildType = ddk::Device<SpiChild, ddk::Messageable>;

class SpiChild : public SpiChildType,
                 public fbl::RefCounted<SpiChild>,
                 public llcpp::fuchsia::hardware::spi::Device::Interface,
                 public ddk::SpiProtocol<SpiChild, ddk::base_protocol> {
 public:
  SpiChild(zx_device_t* parent, ddk::SpiImplProtocolClient spi, const spi_channel_t* channel,
           SpiDevice* spi_parent)
      : SpiChildType(parent), spi_(spi), cs_(channel->cs), spi_parent_(*spi_parent) {}

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void TransmitVector(::fidl::VectorView<uint8_t> data,
                      TransmitVectorCompleter::Sync& completer) override;
  void ReceiveVector(uint32_t size, ReceiveVectorCompleter::Sync& completer) override;
  void ExchangeVector(::fidl::VectorView<uint8_t> txdata,
                      ExchangeVectorCompleter::Sync& completer) override;

  void RegisterVmo(uint32_t vmo_id, ::llcpp::fuchsia::mem::Range vmo,
                   ::llcpp::fuchsia::hardware::sharedmemory::SharedVmoRight rights,
                   RegisterVmoCompleter::Sync& completer) override;
  void UnregisterVmo(uint32_t vmo_id, UnregisterVmoCompleter::Sync& completer) override;

  void Transmit(::llcpp::fuchsia::hardware::sharedmemory::SharedVmoBuffer buffer,
                TransmitCompleter::Sync& completer) override;
  void Receive(::llcpp::fuchsia::hardware::sharedmemory::SharedVmoBuffer buffer,
               ReceiveCompleter::Sync& completer) override;
  void Exchange(::llcpp::fuchsia::hardware::sharedmemory::SharedVmoBuffer tx_buffer,
                ::llcpp::fuchsia::hardware::sharedmemory::SharedVmoBuffer rx_buffer,
                ExchangeCompleter::Sync& completer) override;

  zx_status_t SpiTransmit(const uint8_t* txdata_list, size_t txdata_count);
  zx_status_t SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                         size_t* out_rxdata_actual);
  zx_status_t SpiExchange(const uint8_t* txdata_list, size_t txdata_count, uint8_t* out_rxdata_list,
                          size_t rxdata_count, size_t* out_rxdata_actual);
  void SpiConnectServer(zx::channel server);

 private:
  const ddk::SpiImplProtocolClient spi_;
  const uint32_t cs_;
  SpiDevice& spi_parent_;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_SPI_SPI_CHILD_H_
