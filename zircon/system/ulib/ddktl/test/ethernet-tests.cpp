// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

namespace {

// These tests are testing interfaces that get included via multiple inheritance, and thus we must
// make sure we get all the casts correct. We record the value of the "this" pointer in the
// constructor, and then verify in each call the "this" pointer was the same as the original. (The
// typical way for this to go wrong is to take a EthmacIfc<D>* instead of a D* in a function
// signature.)
#define get_this() reinterpret_cast<uintptr_t>(this)

class TestEthmacIfc : public ddk::Device<TestEthmacIfc>,
                      public ddk::EthmacIfcProtocol<TestEthmacIfc> {
public:
    TestEthmacIfc()
        : ddk::Device<TestEthmacIfc>(nullptr) {
        this_ = get_this();
    }

    void DdkRelease() {}

    void EthmacIfcStatus(uint32_t status) {
        status_this_ = get_this();
        status_called_ = true;
    }

    void EthmacIfcRecv(const void* data, size_t length, uint32_t flags) {
        recv_this_ = get_this();
        recv_called_ = true;
    }

    void EthmacIfcCompleteTx(ethmac_netbuf_t* netbuf, zx_status_t status) {
        complete_tx_this_ = get_this();
        complete_tx_called_ = true;
    }

    bool VerifyCalls() const {
        BEGIN_HELPER;
        EXPECT_EQ(this_, status_this_, "");
        EXPECT_EQ(this_, recv_this_, "");
        EXPECT_EQ(this_, complete_tx_this_, "");
        EXPECT_TRUE(status_called_, "");
        EXPECT_TRUE(recv_called_, "");
        EXPECT_TRUE(complete_tx_called_, "");
        END_HELPER;
    }

    ethmac_ifc_protocol_t ethmac_ifc() { return {&ethmac_ifc_protocol_ops_, this}; }

    zx_status_t StartProtocol(ddk::EthmacProtocolClient* client) {
        return client->Start(this, &ethmac_ifc_protocol_ops_);
    }

private:
    uintptr_t this_ = 0u;
    uintptr_t status_this_ = 0u;
    uintptr_t recv_this_ = 0u;
    uintptr_t complete_tx_this_ = 0u;
    bool status_called_ = false;
    bool recv_called_ = false;
    bool complete_tx_called_ = false;
};

class TestEthmacProtocol : public ddk::Device<TestEthmacProtocol, ddk::GetProtocolable>,
                           public ddk::EthmacProtocol<TestEthmacProtocol, ddk::base_protocol> {
public:
    TestEthmacProtocol()
        : ddk::Device<TestEthmacProtocol, ddk::GetProtocolable>(nullptr) {
        this_ = get_this();
    }

    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out) {
        if (proto_id != ZX_PROTOCOL_ETHMAC)
            return ZX_ERR_INVALID_ARGS;
        ddk::AnyProtocol* proto = static_cast<ddk::AnyProtocol*>(out);
        proto->ops = &ethmac_protocol_ops_;
        proto->ctx = this;
        return ZX_OK;
    }

    void DdkRelease() {}

    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info) {
        query_this_ = get_this();
        query_called_ = true;
        return ZX_OK;
    }

    void EthmacStop() {
        stop_this_ = get_this();
        stop_called_ = true;
    }

    zx_status_t EthmacStart(const ethmac_ifc_protocol_t* ifc) {
        start_this_ = get_this();
        client_ = fbl::make_unique<ddk::EthmacIfcProtocolClient>(ifc);
        start_called_ = true;
        return ZX_OK;
    }

    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
        queue_tx_this_ = get_this();
        queue_tx_called_ = true;
        return ZX_OK;
    }

    zx_status_t EthmacSetParam(uint32_t param, int32_t value, const void* data, size_t data_size) {
        set_param_this_ = get_this();
        set_param_called_ = true;
        return ZX_OK;
    }
    void EthmacGetBti(zx::bti* bti) { bti->reset(); }

    bool VerifyCalls() const {
        BEGIN_HELPER;
        EXPECT_EQ(this_, query_this_, "");
        EXPECT_EQ(this_, start_this_, "");
        EXPECT_EQ(this_, stop_this_, "");
        EXPECT_EQ(this_, queue_tx_this_, "");
        EXPECT_EQ(this_, set_param_this_, "");
        EXPECT_TRUE(query_called_, "");
        EXPECT_TRUE(start_called_, "");
        EXPECT_TRUE(stop_called_, "");
        EXPECT_TRUE(queue_tx_called_, "");
        EXPECT_TRUE(set_param_called_, "");
        END_HELPER;
    }

    bool TestIfc() {
        if (!client_)
            return false;
        // Use the provided client to test the ifc client.
        client_->Status(0);
        client_->Recv(nullptr, 0, 0);
        client_->CompleteTx(nullptr, ZX_OK);
        return true;
    }

private:
    uintptr_t this_ = 0u;
    uintptr_t query_this_ = 0u;
    uintptr_t stop_this_ = 0u;
    uintptr_t start_this_ = 0u;
    uintptr_t queue_tx_this_ = 0u;
    uintptr_t set_param_this_ = 0u;
    bool query_called_ = false;
    bool stop_called_ = false;
    bool start_called_ = false;
    bool queue_tx_called_ = false;
    bool set_param_called_ = false;

    fbl::unique_ptr<ddk::EthmacIfcProtocolClient> client_;
};

static bool test_ethmac_ifc() {
    BEGIN_TEST;

    TestEthmacIfc dev;

    auto ifc = dev.ethmac_ifc();
    ethmac_ifc_status(&ifc, 0);
    ethmac_ifc_recv(&ifc, nullptr, 0, 0);
    ethmac_ifc_complete_tx(&ifc, nullptr, ZX_OK);

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_ifc_client() {
    BEGIN_TEST;

    TestEthmacIfc dev;
    const ethmac_ifc_protocol_t ifc = dev.ethmac_ifc();
    ddk::EthmacIfcProtocolClient client(&ifc);

    client.Status(0);
    client.Recv(nullptr, 0, 0);
    client.CompleteTx(nullptr, ZX_OK);

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol() {
    BEGIN_TEST;

    TestEthmacProtocol dev;

    // Normally we would use device_op_get_protocol, but we haven't added the device to devmgr so
    // its ops table is currently invalid.
    ethmac_protocol_t proto;
    auto status = dev.DdkGetProtocol(0, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "");

    status = dev.DdkGetProtocol(ZX_PROTOCOL_ETHMAC, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");

    EXPECT_EQ(ZX_OK, ethmac_query(&proto, 0, nullptr), "");
    proto.ops->stop(proto.ctx);
    ethmac_ifc_protocol_t ifc = {nullptr, nullptr};
    EXPECT_EQ(ZX_OK, ethmac_start(&proto, ifc.ctx, ifc.ops), "");
    ethmac_netbuf_t netbuf = {};
    EXPECT_EQ(ZX_OK, ethmac_queue_tx(&proto, 0, &netbuf), "");
    EXPECT_EQ(ZX_OK, ethmac_set_param(&proto, 0, 0, nullptr, 0), "");

    EXPECT_TRUE(dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol_client() {
    BEGIN_TEST;

    // The EthmacProtocol device to wrap. This would live in the parent device
    // our driver was binding to.
    TestEthmacProtocol protocol_dev;

    ethmac_protocol_t proto;
    auto status = protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHMAC, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");
    // The client device to wrap the ops + device that represent the parent
    // device.
    ddk::EthmacProtocolClient client(&proto);
    // The EthmacIfc to hand to the parent device.
    TestEthmacIfc ifc_dev;
    ethmac_ifc_protocol_t ifc = ifc_dev.ethmac_ifc();

    EXPECT_EQ(ZX_OK, client.Query(0, nullptr), "");
    client.Stop();
    EXPECT_EQ(ZX_OK, client.Start(ifc.ctx, ifc.ops), "");
    ethmac_netbuf_t netbuf = {};
    EXPECT_EQ(ZX_OK, client.QueueTx(0, &netbuf), "");
    EXPECT_EQ(ZX_OK, client.SetParam(0, 0, nullptr, 0));

    EXPECT_TRUE(protocol_dev.VerifyCalls(), "");

    END_TEST;
}

static bool test_ethmac_protocol_ifc_client() {
    BEGIN_TEST;

    // We create a protocol device that we will start from an ifc device. The protocol device will
    // then use the pointer passed to it to call methods on the ifc device. This ensures the void*
    // casting is correct.
    TestEthmacProtocol protocol_dev;

    ethmac_protocol_t proto;
    auto status = protocol_dev.DdkGetProtocol(ZX_PROTOCOL_ETHMAC, reinterpret_cast<void*>(&proto));
    EXPECT_EQ(ZX_OK, status, "");

    ddk::EthmacProtocolClient client(&proto);
    TestEthmacIfc ifc_dev;
    EXPECT_EQ(ZX_OK, ifc_dev.StartProtocol(&client), "");

    // Execute the EthmacIfc methods
    ASSERT_TRUE(protocol_dev.TestIfc(), "");
    // Verify that they were called
    EXPECT_TRUE(ifc_dev.VerifyCalls(), "");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(ddktl_ethernet_device)
RUN_NAMED_TEST("ddk::EthmacIfcProtocol", test_ethmac_ifc);
RUN_NAMED_TEST("ddk::EthmacIfcProtocolClient", test_ethmac_ifc_client);
RUN_NAMED_TEST("ddk::EthmacProtocol", test_ethmac_protocol);
RUN_NAMED_TEST("ddk::EthmacProtocolClient", test_ethmac_protocol_client);
RUN_NAMED_TEST("EthmacProtocol using EthmacIfcClient", test_ethmac_protocol_ifc_client);
END_TEST_CASE(ddktl_ethernet_device)

test_case_element* test_case_ddktl_ethernet_device = TEST_CASE_ELEMENT(ddktl_ethernet_device);
