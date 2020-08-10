// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/spi/spi.h>

#include <ddk/metadata.h>
#include <ddk/metadata/spi.h>
#include <ddktl/protocol/platform/bus.h>
#include <zxtest/zxtest.h>

namespace spi {

class FakeDdkSpiImpl;
using DeviceType = ddk::Device<FakeDdkSpiImpl, ddk::UnbindableDeprecated>;

class FakeDdkSpiImpl : public fake_ddk::Bind,
                       public DeviceType,
                       public ddk::SpiImplProtocol<FakeDdkSpiImpl, ddk::base_protocol> {
 public:
  explicit FakeDdkSpiImpl() : DeviceType(fake_ddk::kFakeParent) {
    fbl::AllocChecker ac;
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new (&ac) fake_ddk::ProtocolEntry[1](), 1);
    ASSERT_TRUE(ac.check());
    protocols[0] = {ZX_PROTOCOL_SPI_IMPL, {&spi_impl_protocol_ops_, this}};
    SetProtocols(std::move(protocols));
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) {
    if (parent == fake_ddk::kFakeParent) {
      bus_device_ = reinterpret_cast<SpiDevice*>(args->ctx);
    } else if (parent == reinterpret_cast<zx_device_t*>(bus_device_)) {
      children_.push_back(reinterpret_cast<SpiChild*>(args->ctx));

      auto fidl = new fake_ddk::FidlMessenger;
      const zx_protocol_device_t* ops = reinterpret_cast<const zx_protocol_device_t*>(args->ops);
      fidl->SetMessageOp(args->ctx, ops->message);
      fidl_clients_.push_back(fidl);
    }
    *out = reinterpret_cast<zx_device_t*>(args->ctx);
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) {
    if (device == reinterpret_cast<zx_device_t*>(bus_device_)) {
      delete bus_device_;
      bus_device_ = nullptr;

      // The SpiChild device's unbind hooks will be called after the
      // Spi device has replied to its unbind.
      // We need to make a copy before iterating, as the SpiChild will be
      // erased from this |children_| list when it replies to its unbind.
      fbl::Vector<SpiChild*> children_copy;
      for (size_t i = 0; i < children_.size(); i++) {
        children_copy.push_back(children_[i]);
      }
      for (size_t i = 0; i < children_copy.size(); i++) {
        zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(children_copy[i]);
        children_copy[i]->DdkUnbindNew(ddk::UnbindTxn(zxdev));
      }
      return ZX_OK;
    } else {
      for (size_t i = 0; i < children_.size(); i++) {
        if (children_[i] == reinterpret_cast<SpiChild*>(device)) {
          children_.erase(i);
          delete fidl_clients_[i];
          fidl_clients_.erase(i);
          return ZX_OK;
        }
      }
    }
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                size_t* actual) {
    switch (type) {
      case DEVICE_METADATA_SPI_CHANNELS:
        memcpy(buf, &spi_channels_, sizeof spi_channels_);
        *actual = sizeof spi_channels_;
        break;
      case DEVICE_METADATA_PRIVATE:
        memcpy(buf, &kTestBusId, sizeof kTestBusId);
        *actual = sizeof kTestBusId;
        break;
      default:
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  }

  zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) {
    switch (type) {
      case DEVICE_METADATA_SPI_CHANNELS:
        *out_size = sizeof spi_channels_;
        break;
      case DEVICE_METADATA_PRIVATE:
        *out_size = sizeof kTestBusId;
        break;
      default:
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  }

  uint32_t SpiImplGetChipSelectCount() { return 2; }

  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual) {
    EXPECT_EQ(cs, current_test_cs_, "");

    switch (test_mode_) {
      case SpiTestMode::kTransmit:
        EXPECT_NE(txdata, nullptr, "");
        EXPECT_NE(txdata_size, 0, "");
        EXPECT_EQ(out_rxdata, nullptr, "");
        EXPECT_EQ(rxdata_size, 0, "");
        break;
      case SpiTestMode::kReceive:
        EXPECT_EQ(txdata, nullptr, "");
        EXPECT_EQ(txdata_size, 0, "");
        EXPECT_NE(out_rxdata, nullptr, "");
        EXPECT_NE(rxdata_size, 0, "");
        memset(out_rxdata, 0, rxdata_size);
        *out_rxdata_actual = rxdata_size;
        break;
      case SpiTestMode::kExchange:
        EXPECT_NE(txdata, nullptr, "");
        EXPECT_NE(txdata_size, 0, "");
        EXPECT_NE(out_rxdata, nullptr, "");
        EXPECT_NE(rxdata_size, 0, "");
        EXPECT_EQ(txdata_size, rxdata_size, "");
        memset(out_rxdata, 0, rxdata_size);
        *out_rxdata_actual = rxdata_size;
        break;
    }

    return ZX_OK;
  }

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  SpiDevice* bus_device_;
  fbl::Vector<SpiChild*> children_;
  fbl::Vector<fake_ddk::FidlMessenger*> fidl_clients_;
  uint32_t current_test_cs_;

  enum class SpiTestMode {
    kTransmit,
    kReceive,
    kExchange,
  } test_mode_;

  static constexpr uint32_t kTestBusId = 0;
  static constexpr spi_channel_t spi_channels_[] = {
      {.bus_id = 0, .cs = 0, .vid = 0, .pid = 0, .did = 0},
      {.bus_id = 0, .cs = 1, .vid = 0, .pid = 0, .did = 0}};
};

TEST(SpiDevice, SpiTest) {
  FakeDdkSpiImpl ddk;

  // make it
  SpiDevice::Create(nullptr, fake_ddk::kFakeParent);
  EXPECT_EQ(ddk.children_.size(), countof(ddk.spi_channels_), "");

  // test it
  const uint8_t txbuf[] = {0, 1, 2, 3, 4, 5, 6};
  uint8_t rxbuf[sizeof txbuf];

  for (size_t i = 0; i < ddk.children_.size(); i++) {
    ddk.current_test_cs_ = static_cast<uint32_t>(i);
    auto channel = ddk.fidl_clients_[i]->local().get();

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kTransmit;
    zx_status_t status = spilib_transmit(channel, txbuf, sizeof txbuf);
    EXPECT_OK(status, "");

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kReceive;
    status = spilib_receive(channel, rxbuf, sizeof rxbuf);
    EXPECT_OK(status, "");

    ddk.test_mode_ = FakeDdkSpiImpl::SpiTestMode::kExchange;
    status = spilib_exchange(channel, txbuf, rxbuf, sizeof txbuf);
    EXPECT_OK(status, "");
  }

  // clean it up
  zx_device_t* zxdev = reinterpret_cast<zx_device_t*>(ddk.bus_device_);
  ddk.bus_device_->DdkUnbindNew(ddk::UnbindTxn(zxdev));
  EXPECT_EQ(ddk.children_.size(), 0, "");
  EXPECT_EQ(ddk.bus_device_, nullptr, "");
}

}  // namespace spi
