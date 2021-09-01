// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERTAP_ETHERTAP_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERTAP_ETHERTAP_H_

#include <fidl/fuchsia.hardware.ethertap/cpp/wire.h>
#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/ethertap/c/fidl.h>
#include <lib/ddk/device.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/mutex.h>

class EthertapTests;

namespace eth {

class TapCtl;
using DeviceType =
    ddk::Device<TapCtl, ddk::Messageable<fuchsia_hardware_ethertap::TapControl>::Mixin>;

class TapCtl : public DeviceType {
 public:
  TapCtl(zx_device_t* device);

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  void DdkRelease();
  zx_status_t OpenDeviceInternal(const char* name,
                                 const fuchsia_hardware_ethertap::wire::Config& config,
                                 fidl::ServerEnd<fuchsia_hardware_ethertap::TapDevice> device);
  void OpenDevice(OpenDeviceRequestView request, OpenDeviceCompleter::Sync& completer) override;

 private:
  friend ::EthertapTests;
};

class TapDevice : public ddk::Device<TapDevice, ddk::Unbindable>,
                  public ddk::EthernetImplProtocol<TapDevice, ddk::base_protocol> {
 public:
  TapDevice(zx_device_t* device, const fuchsia_hardware_ethertap::wire::Config& config,
            fidl::ServerEnd<fuchsia_hardware_ethertap::TapDevice> server);

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop();
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                   size_t data_size);
  // No DMA capability, so return invalid handle for get_bti
  void EthernetImplGetBti(zx::bti* bti);
  int Thread();
  zx_status_t Reply(zx_txid_t, const fidl_outgoing_msg_t* msg);

  zx_status_t Recv(const uint8_t* buffer, uint32_t length);
  void UpdateLinkStatus(bool online);

 private:
  // ethertap options
  uint32_t options_ = 0;

  // ethermac fields
  uint32_t features_ = 0;
  uint32_t mtu_ = 0;
  uint8_t mac_[6] = {};

  fbl::Mutex lock_;
  bool dead_ __TA_GUARDED(lock_) = false;
  ddk::EthernetIfcProtocolClient ethernet_client_ __TA_GUARDED(lock_);
  std::optional<ddk::UnbindTxn> unbind_txn_ __TA_GUARDED(lock_);

  // Only accessed from Thread, so not locked.
  bool online_ = false;
  zx::channel channel_;

  thrd_t thread_;
};

}  // namespace eth

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERTAP_ETHERTAP_H_
