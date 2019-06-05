// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/process.h>
#include <thread>
#include <zxtest/zxtest.h>

class FakeEthmacProtocol : public ddk::Device<FakeEthmacProtocol, ddk::GetProtocolable>,
                           public ddk::EthmacProtocol<FakeEthmacProtocol, ddk::base_protocol> {
public:
    FakeEthmacProtocol()
        : ddk::Device<FakeEthmacProtocol, ddk::GetProtocolable>(fake_ddk::kFakeDevice),
          proto_({&ethmac_protocol_ops_, this}) {
    }

    const ethmac_protocol_t* proto() const { return &proto_; }

    void DdkRelease() {}

    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info) {
        info->netbuf_size = sizeof(ethmac_netbuf_t);
        info->mtu = 1500;
        memcpy(info->mac, mac_, sizeof(info->mac));
        return ZX_OK;
    }

    void EthmacStop() {}

    zx_status_t EthmacStart(const ethmac_ifc_protocol_t* ifc) {
        client_ = std::make_unique<ddk::EthmacIfcProtocolClient>(ifc);
        return ZX_OK;
    }

    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
        queue_tx_called_ = true;
        return ZX_OK;
    }

    zx_status_t EthmacSetParam(uint32_t param, int32_t value, const void* data, size_t data_size) {
        if (param == ETHMAC_SETPARAM_DUMP_REGS) {
            dump_called_ = true;
        }
        return ZX_OK;
    }
    void EthmacGetBti(zx::bti* bti) { bti->reset(); }

    bool TestInfo(fuchsia_hardware_ethernet_Info* info) {
        if (memcmp(mac_, info->mac.octets, ETH_MAC_SIZE) || (info->mtu != 1500)) {
            return false;
        }
        return true;
    }

    bool TestDump() { return dump_called_; }

    bool TestIfc() {
        if (!client_)
            return false;
        // Use the provided client to test the ifc client.
        client_->Status(0);
        client_->Recv(nullptr, 0, 0);
        client_->CompleteTx(nullptr, ZX_OK);
        return true;
    }

    bool SetStatus(uint32_t status) {
        if (!client_)
            return false;
        client_->Status(status);
        return true;
    }

    bool TestQueueTx() {
        return queue_tx_called_;
    }

    bool TestRecv() {
        if (!client_) {
            return false;
        }
        uint8_t data = 0xAA;
        client_->Recv(&data, 1, 0);
        return true;
    }

private:
    ethmac_protocol_t proto_;
    const uint8_t mac_[ETH_MAC_SIZE] = {0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
    fbl::unique_ptr<ddk::EthmacIfcProtocolClient> client_;

    bool dump_called_ = false;
    bool queue_tx_called_ = false;
};

class EthernetTester {
public:
    EthernetTester() {
        fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
        protocols[0] = {ZX_PROTOCOL_ETHMAC,
                        *reinterpret_cast<const fake_ddk::Protocol*>(ethmac_.proto())};
        ddk_.SetProtocols(std::move(protocols));
    }

    fake_ddk::Bind& ddk() { return ddk_; }
    FakeEthmacProtocol& ethmac() { return ethmac_; }

private:
    fake_ddk::Bind ddk_;
    FakeEthmacProtocol ethmac_;
};

TEST(EthernetTest, BindTest) {
    EthernetTester tester;
    EXPECT_EQ(eth::EthDev0::EthBind(nullptr, fake_ddk::kFakeParent), ZX_OK, "Bind failed");
}

TEST(EthernetTest, DdkLifecycleTest) {
    EthernetTester tester;
    eth::EthDev0* eth(new eth::EthDev0(fake_ddk::kFakeParent));
    EXPECT_EQ(eth->AddDevice(), ZX_OK, "AddDevice Failed");
    eth->DdkUnbind();
    EXPECT_TRUE(tester.ddk().Ok());
    eth->DdkRelease();
}

TEST(EthernetTest, OpenTest) {
    EthernetTester tester;
    eth::EthDev0* eth(new eth::EthDev0(fake_ddk::kFakeParent));
    EXPECT_EQ(eth->AddDevice(), ZX_OK, "AddDevice Failed");
    zx_device_t* eth_instance;
    EXPECT_EQ(eth->DdkOpen(&eth_instance, 0), ZX_OK, "Open Failed");
    eth->DdkUnbind();
    eth->DdkRelease();
}

class EthernetDeviceTest {
public:
    EthernetDeviceTest()
        : tester() {

        edev0 = std::make_unique<eth::EthDev0>(fake_ddk::kFakeParent);
        ASSERT_OK(edev0->AddDevice());

        edev = fbl::MakeRefCounted<eth::EthDev>(fake_ddk::kFakeParent, edev0.get());
        zx_device_t* out;
        ASSERT_OK(edev->AddDevice(&out));
    }

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
        ASSERT_OK(zx::vmo::create(2 * sizeof(ethmac_netbuf_t), ZX_VMO_NON_RESIZABLE, &buf_));
        ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetIOBuffer(FidlChannel(),
                                                              buf_.get(), &out_status));
        ASSERT_OK(out_status);
        ASSERT_OK(fuchsia_hardware_ethernet_DeviceStart(FidlChannel(), &out_status));
        ASSERT_OK(out_status);
    }

    zx_handle_t FidlChannel() { return tester.ddk().FidlClient().get(); }

    zx::fifo& TransmitFifo() { return tx_fifo_; }

    zx::fifo& ReceiveFifo() { return rx_fifo_; }

    EthernetTester tester;
    fbl::unique_ptr<eth::EthDev0> edev0;
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
    EXPECT_EQ(test.edev->DdkOpen(nullptr, 0), ZX_OK, "Instance 1 open failed");
    EXPECT_EQ(test.edev->DdkOpen(nullptr, 0), ZX_OK, "Instance 2 open failed");
    EXPECT_EQ(test.edev->DdkClose(0), ZX_OK, "Instance 0 close failed");
    EXPECT_EQ(test.edev->DdkClose(0), ZX_OK, "Instance 1 close failed");
    EXPECT_EQ(test.edev->DdkClose(0), ZX_OK, "Instance 2 close failed");
}

TEST(EthernetTest, SetClientNameTest) {
    EthernetDeviceTest test;
    zx_status_t call_status = ZX_OK;
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetClientName(test.FidlChannel(),
                                                            "ethtest",
                                                            strlen("ethtest"),
                                                            &call_status));
    ASSERT_OK(call_status);
}

TEST(EthernetTest, GetInfoTest) {
    EthernetDeviceTest test;
    fuchsia_hardware_ethernet_Info info = {};
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetInfo(test.FidlChannel(),
                                                      &info));
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

TEST(EthernetTest, SetPromiscuousModeTest) {
    EthernetDeviceTest test;
    zx_status_t out_status = ZX_ERR_INTERNAL;

    ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(test.FidlChannel(),
                                                                 true, &out_status));
    ASSERT_OK(out_status);

    out_status = ZX_ERR_INTERNAL;
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetPromiscuousMode(test.FidlChannel(),
                                                                 false, &out_status));
    ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastAddMacTest) {
    EthernetDeviceTest test;
    zx_status_t out_status = ZX_ERR_INTERNAL;
    /* 1st bit should be 1 in multicast */
    fuchsia_hardware_ethernet_MacAddress wrong_addr =
        {.octets = {0x00, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(test.FidlChannel(),
                                                                    &wrong_addr, &out_status));
    ASSERT_OK(!out_status);
    fuchsia_hardware_ethernet_MacAddress right_addr =
        {.octets = {0x01, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastAddMac(test.FidlChannel(),
                                                                    &right_addr, &out_status));
    ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastDeleteMacTest) {
    EthernetDeviceTest test;
    zx_status_t out_status = ZX_ERR_INTERNAL;
    fuchsia_hardware_ethernet_MacAddress addr = {.octets = {0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc}};
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastDeleteMac(test.FidlChannel(),
                                                                       &addr, &out_status));
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
                                                                                false,
                                                                                &out_status));
    ASSERT_OK(out_status);
}

TEST(EthernetTest, ConfigMulticastTestFilterTest) {
    EthernetDeviceTest test;
    zx_status_t out_status = ZX_ERR_INTERNAL;
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceConfigMulticastTestFilter(test.FidlChannel(),
                                                                        &out_status));
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
    EXPECT_TRUE(fuchsia_hardware_ethernet_DeviceSetIOBuffer(test.FidlChannel(),
                                                            ZX_HANDLE_INVALID,
                                                            &out_status) != ZX_OK);
    EXPECT_TRUE(out_status != ZX_OK);
    zx::vmo buf;
    ASSERT_OK(zx::vmo::create(2 * sizeof(ethmac_netbuf_t), ZX_VMO_NON_RESIZABLE, &buf));
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceSetIOBuffer(test.FidlChannel(),
                                                          buf.get(), &out_status));
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

    //start device
    test.Start();

    //set mock ethmac status
    EXPECT_TRUE(test.tester.ethmac().SetStatus(1));

    //verify status
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceGetStatus(test.FidlChannel(), &device_status));
    EXPECT_TRUE(device_status == 1);
}

#if 0
// TODO(CONN-135)
TEST(EthernetTest, SendTest) {
    EthernetDeviceTest test;

    //start device
    test.Start();

    //send packet through the fifo
    zx::fifo& tx = test.TransmitFifo();
    eth_fifo_entry_t entry = {
        .offset = 0,
        .length = 1,
        .flags = 0,
        .cookie = 0,
    };
    ASSERT_OK(tx.write(sizeof(entry), &entry, 1, nullptr));

    //wait for packet to be returned
    ASSERT_OK(tx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(tx.read(sizeof(entry), &entry, 1, nullptr));

    //check mock ethmac if packet was received
    EXPECT_TRUE(test.tester.ethmac().TestQueueTx());
}


TEST(EthernetTest, ReceiveTest) {
    EthernetDeviceTest test;

    //start device
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

    //send packet through mock ethmac
    EXPECT_TRUE(test.tester.ethmac().TestRecv());

    //check if packet is received
    ASSERT_OK(rx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
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
    zx::fifo& tx = test.TransmitFifo();
    ASSERT_OK(tx.write(sizeof(entry), &entry, 1, nullptr));

    zx::fifo& rx = test.ReceiveFifo();
    ASSERT_OK(rx.write(sizeof(entry), &entry, 1, nullptr));

    //wait for the send to complete
    ASSERT_OK(tx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(tx.read(sizeof(entry), &entry, 1, nullptr));

    // check if it was echoed
    ASSERT_OK(rx.wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(rx.read(sizeof(entry), &entry, 1, nullptr));
}
#endif

TEST(EthernetTest, ListenStopTest) {
    EthernetDeviceTest test;
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceListenStop(test.FidlChannel()));
}

TEST(EthernetTest, StopTest) {
    EthernetDeviceTest test;
    test.Start();
    ASSERT_OK(fuchsia_hardware_ethernet_DeviceStop(test.FidlChannel()));
}
