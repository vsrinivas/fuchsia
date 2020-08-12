// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/process.h>

#include <memory>
#include <thread>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <zxtest/zxtest.h>

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

  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
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

  bool TestInfo(fuchsia_hardware_ethernet_Info* info) {
    if (memcmp(mac_, info->mac.octets, ETH_MAC_SIZE) || (info->mtu != 1500)) {
      return false;
    }
    return true;
  }

  bool TestDump() { return dump_called_; }

  int32_t TestPromiscuous() { return promiscuous_; }

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

  bool TestQueueTx() { return queue_tx_called_; }

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

class EthernetTester : fake_ddk::Bind {
 public:
  EthernetTester() : fake_ddk::Bind() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_ETHERNET_IMPL,
                    *reinterpret_cast<const fake_ddk::Protocol*>(ethernet_.proto())};
    SetProtocols(std::move(protocols));
  }

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

TEST(EthernetTest, BindTest) {
  EthernetTester tester;
  EXPECT_OK(eth::EthDev0::EthBind(nullptr, fake_ddk::kFakeParent), "Bind failed");
  tester.eth0()->DdkRelease();
}

TEST(EthernetTest, DdkLifecycleTest) {
  EthernetTester tester;
  eth::EthDev0* eth(new eth::EthDev0(fake_ddk::kFakeParent));
  EXPECT_OK(eth->AddDevice(), "AddDevice Failed");
  eth->DdkAsyncRemove();
  EXPECT_TRUE(tester.ddk().Ok());
  eth->DdkRelease();
}

TEST(EthernetTest, OpenTest) {
  EthernetTester tester;
  eth::EthDev0* eth(new eth::EthDev0(fake_ddk::kFakeParent));
  EXPECT_OK(eth->AddDevice(), "AddDevice Failed");
  zx_device_t* eth_instance;
  EXPECT_OK(eth->DdkOpen(&eth_instance, 0), "Open Failed");
  eth->DdkAsyncRemove();
  EXPECT_OK(tester.ddk().WaitUntilRemove());
  eth->DdkRelease();
  tester.instances()[0]->DdkRelease();
}

class EthDev0ForTest : public eth::EthDev0 {
 public:
  EthDev0ForTest(zx_device_t* parent) : eth::EthDev0(parent) {}
  using eth::EthDev0::DestroyAllEthDev;
};

class EthernetDeviceTest {
 public:
  EthernetDeviceTest() : tester() {
    edev0 = std::make_unique<EthDev0ForTest>(fake_ddk::kFakeParent);
    ASSERT_OK(edev0->AddDevice());

    edev = fbl::MakeRefCounted<eth::EthDev>(fake_ddk::kFakeParent, edev0.get());
    zx_device_t* out;
    ASSERT_OK(edev->AddDevice(&out));
  }

  ~EthernetDeviceTest() { edev0->DestroyAllEthDev(); }

  void Start() {
    zx_status_t out_status = ZX_ERR_INTERNAL;
    fuchsia_hardware_ethernet_Fifos fifos = {};
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetFifos(FidlChannel(), &out_status, &fifos));
    tx_fifo_ = zx::fifo(fifos.tx);
    EXPECT_TRUE(tx_fifo_.is_valid());
    rx_fifo_ = zx::fifo(fifos.rx);
    rx_fifo_depth_ = fifos.rx_depth;
    tx_fifo_depth_ = fifos.tx_depth;
    EXPECT_TRUE(rx_fifo_.is_valid());
    ASSERT_OK(zx::vmo::create(2 * sizeof(ethernet_netbuf_t), 0, &buf_));
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetIOBuffer(FidlChannel(), buf_.get(), &out_status));
    ASSERT_OK(out_status);
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceStart(FidlChannel(), &out_status));
    ASSERT_OK(out_status);
  }

  zx_handle_t FidlChannel() { return tester.ddk().FidlClient().get(); }

  zx::fifo& TransmitFifo() { return tx_fifo_; }

  zx::fifo& ReceiveFifo() { return rx_fifo_; }

  EthernetTester tester;
  std::unique_ptr<EthDev0ForTest> edev0;
  fbl::RefPtr<eth::EthDev> edev;

 private:
  zx::fifo tx_fifo_;
  zx::fifo rx_fifo_;
  uint32_t rx_fifo_depth_;
  uint32_t tx_fifo_depth_;
  zx::vmo buf_;
};

TEST(EthernetTest, MultipleOpenTest) {
  EthernetDeviceTest test;
  EXPECT_OK(test.edev->DdkOpen(nullptr, 0), "Instance 1 open failed");
  EXPECT_OK(test.edev->DdkOpen(nullptr, 0), "Instance 2 open failed");
  EXPECT_OK(test.edev->DdkClose(0), "Instance 0 close failed");
  EXPECT_OK(test.edev->DdkClose(0), "Instance 1 close failed");
  EXPECT_OK(test.edev->DdkClose(0), "Instance 2 close failed");
}

TEST(EthernetTest, SetClientNameTest) {
  EthernetDeviceTest test;
  zx_status_t call_status = ZX_OK;
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetClientName(test.FidlChannel(), "ethtest",
                                                          strlen("ethtest"), &call_status));
  ASSERT_OK(call_status);
}

TEST(EthernetTest, GetInfoTest) {
  EthernetDeviceTest test;
  fuchsia_hardware_ethernet_Info info = {};
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetInfo(test.FidlChannel(), &info));
  EXPECT_TRUE(test.tester.ethmac().TestInfo(&info));
}

TEST(EthernetTest, GetFifosTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  fuchsia_hardware_ethernet_Fifos fifos = {};

  ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetFifos(test.FidlChannel(), &out_status, &fifos));
  ASSERT_OK(out_status);
  EXPECT_TRUE(fifos.rx != ZX_HANDLE_INVALID);
  EXPECT_TRUE(fifos.tx != ZX_HANDLE_INVALID);
}

TEST(EthernetTest, AddDeviceAsNotPromiscuous) {
  EthernetDeviceTest test;
  EXPECT_EQ(test.tester.ethmac().TestPromiscuous(), 0, "");
}

TEST(EthernetTest, SetPromiscuousModeTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;

  ASSERT_OK(
      fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(test.FidlChannel(), true, &out_status));
  ASSERT_OK(out_status);
  EXPECT_EQ(test.tester.ethmac().TestPromiscuous(), 1, "");

  out_status = ZX_ERR_INTERNAL;
  ASSERT_OK(
      fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(test.FidlChannel(), false, &out_status));
  ASSERT_OK(out_status);
  EXPECT_EQ(test.tester.ethmac().TestPromiscuous(), 0, "");
}

TEST(EthernetTest, ConfigMulticastAddMacTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  /* 1st bit should be 1 in multicast */
  fuchsia_hardware_ethernet_MacAddress wrong_addr = {
      .octets = {0x00, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(test.FidlChannel(), &wrong_addr,
                                                                  &out_status));
  ASSERT_OK(!out_status);
  fuchsia_hardware_ethernet_MacAddress right_addr = {
      .octets = {0x01, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(test.FidlChannel(), &right_addr,
                                                                  &out_status));
  ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastDeleteMacTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  fuchsia_hardware_ethernet_MacAddress addr = {.octets = {0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastDeleteMac(test.FidlChannel(), &addr,
                                                                     &out_status));
  ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastSetPromiscuousModeTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastSetPromiscuousMode(test.FidlChannel(),
                                                                              true, &out_status));
  ASSERT_OK(out_status);

  out_status = ZX_ERR_INTERNAL;
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastSetPromiscuousMode(test.FidlChannel(),
                                                                              false, &out_status));
  ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastTestFilterTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  ASSERT_OK(
      fuchsia_hardware_ethernet_DeviceConfigMulticastTestFilter(test.FidlChannel(), &out_status));
  ASSERT_OK(out_status);
}

TEST(EthernetTest, DumpRegistersTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceDumpRegisters(test.FidlChannel(), &out_status));
  ASSERT_OK(out_status);
  EXPECT_TRUE(test.tester.ethmac().TestDump());
}

TEST(EthernetTest, SetIOBufferTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;
  EXPECT_TRUE(fuchsia_hardware_ethernet_DeviceSetIOBuffer(test.FidlChannel(), ZX_HANDLE_INVALID,
                                                          &out_status) != ZX_OK);
  EXPECT_TRUE(out_status != ZX_OK);
  zx::vmo buf;
  ASSERT_OK(zx::vmo::create(2 * sizeof(ethernet_netbuf_t), 0, &buf));
  ASSERT_OK(
      fuchsia_hardware_ethernet_DeviceSetIOBuffer(test.FidlChannel(), buf.get(), &out_status));
  ASSERT_OK(out_status);
}

TEST(EthernetTest, StartTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_OK;
  // test bad state
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceStart(test.FidlChannel(), &out_status));
  EXPECT_TRUE(out_status != ZX_OK);

  // test valid case
  test.Start();

  // test client interfaces
  EXPECT_TRUE(test.tester.ethmac().TestIfc());
}

TEST(EthernetTest, GetStatusTest) {
  EthernetDeviceTest test;
  uint32_t device_status;

  // start device
  test.Start();

  // set mock ethmac status
  EXPECT_TRUE(test.tester.ethmac().SetStatus(1));

  // verify status
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetStatus(test.FidlChannel(), &device_status));
  EXPECT_TRUE(device_status == 1);
}

TEST(EthernetTest, SendTest) {
  EthernetDeviceTest test;

  // start device
  test.Start();

  // send packet through the fifo
  zx::fifo& tx = test.TransmitFifo();
  eth_fifo_entry_t entry = {
      .offset = 0,
      .length = 1,
      .flags = 0,
      .cookie = 0,
  };
  ASSERT_OK(tx.write(sizeof(entry), &entry, 1, nullptr));
  // wait for packet to be returned
  ASSERT_OK(tx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
  // TODO(fxbug.dev/21334): remove debug logs after flake fix is confirmed
  printf("SendTest: Transmit wait completed\n");
  ASSERT_OK(tx.read(sizeof(entry), &entry, 1, nullptr));
  // check mock ethmac if packet was received
  EXPECT_TRUE(test.tester.ethmac().TestQueueTx());
}

TEST(EthernetTest, ReceiveTest) {
  EthernetDeviceTest test;

  // start device
  test.Start();

  // Queue buffer to receive fifo
  zx::fifo& rx = test.ReceiveFifo();
  eth_fifo_entry_t entry = {
      .offset = 0,
      .length = 1,
      .flags = 0,
      .cookie = 0,
  };
  ASSERT_OK(rx.write(sizeof(entry), &entry, 1, nullptr));

  // send packet through mock ethmac
  EXPECT_TRUE(test.tester.ethmac().TestRecv());

  // check if packet is received
  ASSERT_OK(rx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
  // TODO(fxbug.dev/21334): remove debug logs after flake fix is confirmed
  printf("ReceiveTest: Receive wait completed\n");
  ASSERT_OK(rx.read(sizeof(entry), &entry, 1, nullptr));
}

TEST(EthernetTest, ListenStartTest) {
  EthernetDeviceTest test;
  zx_status_t out_status = ZX_ERR_INTERNAL;

  // start device
  test.Start();

  // Set listen start
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceListenStart(test.FidlChannel(), &out_status));
  ASSERT_OK(out_status);

  // send packet
  eth_fifo_entry_t entry = {
      .offset = 0,
      .length = 1,
      .flags = 0,
      .cookie = 0,
  };

  zx::fifo& rx = test.ReceiveFifo();
  ASSERT_OK(rx.write(sizeof(entry), &entry, 1, nullptr));

  zx::fifo& tx = test.TransmitFifo();
  ASSERT_OK(tx.write(sizeof(entry), &entry, 1, nullptr));

  // wait for the send to complete
  ASSERT_OK(tx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
  // TODO(fxbug.dev/21334): remove debug logs after flake fix is confirmed
  printf("ListenStartTest: Transmit wait completed\n");
  ASSERT_OK(tx.read(sizeof(entry), &entry, 1, nullptr));
  // check mock ethmac if packet was received
  EXPECT_TRUE(test.tester.ethmac().TestQueueTx());

  // check if it was echoed
  ASSERT_OK(rx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
  // TODO(fxbug.dev/21334): remove debug logs after flake fix is confirmed
  printf("ListenStartTest: Receive wait completed\n");
  ASSERT_OK(rx.read(sizeof(entry), &entry, 1, nullptr));
}

TEST(EthernetTest, ListenStopTest) {
  EthernetDeviceTest test;
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceListenStop(test.FidlChannel()));
}

TEST(EthernetTest, StopTest) {
  EthernetDeviceTest test;
  test.Start();
  ASSERT_OK(fuchsia_hardware_ethernet_DeviceStop(test.FidlChannel()));
}
