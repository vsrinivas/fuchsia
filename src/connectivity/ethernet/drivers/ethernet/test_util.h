// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_TEST_UTIL_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_TEST_UTIL_H_

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/process.h>

#include <memory>
#include <thread>

#include "ethernet.h"

namespace ethernet_testing {

class FakeEthernetImplProtocol
    : public ddk::Device<FakeEthernetImplProtocol, ddk::GetProtocolable>,
      public ddk::EthernetImplProtocol<FakeEthernetImplProtocol, ddk::base_protocol> {
 public:
  FakeEthernetImplProtocol()
      : ddk::Device<FakeEthernetImplProtocol, ddk::GetProtocolable>(fake_ddk::kFakeDevice),
        proto_({&ethernet_impl_protocol_ops_, this}) {}

  const ethernet_impl_protocol_t* proto() const { return &proto_; }

  void DdkRelease() {}

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
    info->netbuf_size = sizeof(ethernet_netbuf_t);
    info->mtu = 1500;
    memcpy(info->mac, mac_, sizeof(info->mac));
    return ZX_OK;
  }

  void EthernetImplStop() {}

  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
    client_ = std::make_unique<ddk::EthernetIfcProtocolClient>(ifc);
    return ZX_OK;
  }

  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
    queue_tx_called_ = true;
    completion_cb(cookie, ZX_OK, netbuf);
  }

  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                   size_t data_size) {
    if (param == ETHERNET_SETPARAM_DUMP_REGS) {
      dump_called_ = true;
    }
    if (param == ETHERNET_SETPARAM_PROMISC) {
      promiscuous_ = value;
    }
    return ZX_OK;
  }

  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

  bool TestInfo(fuchsia_hardware_ethernet::wire::Info* info) {
    return !(memcmp(mac_, info->mac.octets.data(), ETH_MAC_SIZE) || (info->mtu != 1500));
  }

  bool TestDump() const { return dump_called_; }

  int32_t TestPromiscuous() const { return promiscuous_; }

  bool TestIfc() {
    if (!client_)
      return false;
    // Use the provided client to test the ifc client.
    client_->Status(0);
    client_->Recv(nullptr, 0, 0);
    return true;
  }

  bool SetStatus(uint32_t status) {
    if (!client_)
      return false;
    client_->Status(status);
    return true;
  }

  bool TestQueueTx() const { return queue_tx_called_; }

  bool TestRecv() {
    if (!client_) {
      return false;
    }
    uint8_t data = 0xAA;
    client_->Recv(&data, 1, 0);
    return true;
  }

 private:
  ethernet_impl_protocol_t proto_;
  const uint8_t mac_[ETH_MAC_SIZE] = {0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
  std::unique_ptr<ddk::EthernetIfcProtocolClient> client_;

  bool dump_called_ = false;
  int32_t promiscuous_ = -1;
  bool queue_tx_called_ = false;
};

class EthernetTester : public fake_ddk::Bind {
 public:
  EthernetTester() : fake_ddk::Bind() { SetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, ethernet_.proto()); }

  fake_ddk::Bind& ddk() { return *this; }
  FakeEthernetImplProtocol& ethmac() { return ethernet_; }
  eth::EthDev0* eth0() { return eth0_; }
  const std::vector<eth::EthDev*>& instances() { return instances_; }

 protected:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t ret = Bind::DeviceAdd(drv, parent, args, out);
    if (ret == ZX_OK) {
      if (parent == fake_ddk::kFakeParent) {
        eth0_ = static_cast<eth::EthDev0*>(args->ctx);
      } else {
        instances_.push_back(static_cast<eth::EthDev*>(args->ctx));
      }
    }
    return ret;
  }

 private:
  FakeEthernetImplProtocol ethernet_;
  eth::EthDev0* eth0_;
  std::vector<eth::EthDev*> instances_;
};

}  // namespace ethernet_testing

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_TEST_UTIL_H_
